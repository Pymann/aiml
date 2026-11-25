#define DEBUG_MODE
#include "builtin_registry.h"
#include "ast_nodes.h"
#include "buffer_impl.h"
#include "map_impl.h"
#include "list_impl.h"
#include "string_utils.h"
#include "code_impl.h"
#include "ir_generator.h"
#include "path_runtime.h"
#include "../sys/yaml_runtime.h"
#include "../../src/sys/net_runtime.h"
#include "../../src/sys/db_runtime.h"
#include "asset_scanner.h"
#include "../../src/sys/http_runtime.h"
#include "../../src/sys/compress_runtime.h"
#include "../../src/sys/gltf_runtime.h"
#include "../../src/sys/gl_runtime.h"
#include "../../src/sys/engine_runtime.h"
#include "../../src/sys/input_runtime.h"
#include "../../src/sys/ui_runtime.h"
#include "../../src/sys/tensor_runtime.h"
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/GlobalVariable.h>

using IRValue = AIML::IRValue;

// ---------------------------------------------------------------------------
// Helpers used by multiple handlers
// ---------------------------------------------------------------------------
static std::string extractTypeName(AIML::AST::Expr* expr) {
    if (auto* v = dynamic_cast<AIML::AST::VariableExpr*>(expr)) return v->name;
    if (auto* l = dynamic_cast<AIML::AST::LiteralExpr*>(expr))   return l->value;
    return "unknown";
}

// Emit an unconditional __cxa_throw at the current insert point.
// The current basic block MUST be terminated after this call.
static void emitThrow(CompilerContext& ctx, const std::string& excClass) {
    llvm::PointerType* ptrTy = llvm::PointerType::getUnqual(ctx.context);

    llvm::Function* allocFn = ctx.module().getFunction("__cxa_allocate_exception");
    if (!allocFn) {
        auto* fty = llvm::FunctionType::get(ptrTy, {ctx.getSizeType()}, false);
        allocFn = llvm::Function::Create(fty, llvm::Function::ExternalLinkage,
            "__cxa_allocate_exception", ctx.module());
    }
    llvm::Function* throwFn = ctx.module().getFunction("__cxa_throw");
    if (!throwFn) {
        auto* fty = llvm::FunctionType::get(ctx.builder.getVoidTy(), {ptrTy, ptrTy, ptrTy}, false);
        throwFn = llvm::Function::Create(fty, llvm::Function::ExternalLinkage,
            "__cxa_throw", ctx.module());
    }

    // spec/61 §9.3 record layout {object, free thunk, type tag}; a runtime throw has
    // no object and no thunk, but it does have a NAME — spec/22 lists the
    // built-in exceptions, and without a tag no `catch` could select them by type.
    llvm::Value* excAlloc = ctx.builder.CreateCall(allocFn,
        {ctx.getSizeConst(ctx.getExcRecordSize())}, "exc_alloc");
    ctx.builder.CreateStore(llvm::ConstantPointerNull::get(ptrTy),
        ctx.excRecordField(excAlloc, 0, "exc_obj_slot"));
    ctx.builder.CreateStore(llvm::ConstantPointerNull::get(ptrTy),
        ctx.excRecordField(excAlloc, 1, "exc_thunk_slot"));
    ctx.builder.CreateStore(ctx.builder.CreateGlobalStringPtr(excClass, "builtin_exc_tag"),
        ctx.excRecordField(excAlloc, 2, "exc_tag_slot"));
    llvm::Value* null = llvm::ConstantPointerNull::get(ptrTy);
    llvm::Constant* tinfo = ctx.getExcTypeInfo(excClass);

    llvm::BasicBlock* ud = AIML::AST::aimlUnwindDest(ctx, true);
    if (ud) {
        llvm::Function* fn = ctx.builder.GetInsertBlock()->getParent();
        llvm::BasicBlock* unreachBB = llvm::BasicBlock::Create(ctx.context, "throw_unreach", fn);
        ctx.builder.CreateInvoke(throwFn, unreachBB, ud, {excAlloc, tinfo, null});
        ctx.builder.SetInsertPoint(unreachBB);
    } else {
        ctx.builder.CreateCall(throwFn, {excAlloc, tinfo, null});
    }
    ctx.builder.CreateUnreachable();
}

// Like emitThrow, but with a REASON: the throw carries a real object
// whose field `message` points to the given text.  Without this there is a
// null pointer at position [0] — and a `catch` that reads `ex.message` falls
// over (measured 2026-10-25 while building `ctx`, spec/29_2 §14a).
// `codeVal` (i32, optional) fills a SECOND field where the exception class has one:
// spec/33_2 §6.4 gives ZimError a `code` next to `message`, so that a handler can branch
// on the reason without reading the text.
static void emitThrowMsg(CompilerContext& ctx, const std::string& excClass,
                         llvm::Value* msgPtr, llvm::Value* codeVal = nullptr) {
    llvm::PointerType* ptrTy = llvm::PointerType::getUnqual(ctx.context);

    llvm::Function* allocFn = ctx.module().getFunction("__cxa_allocate_exception");
    if (!allocFn) {
        auto* fty = llvm::FunctionType::get(ptrTy, {ctx.getSizeType()}, false);
        allocFn = llvm::Function::Create(fty, llvm::Function::ExternalLinkage,
            "__cxa_allocate_exception", ctx.module());
    }
    llvm::Function* throwFn = ctx.module().getFunction("__cxa_throw");
    if (!throwFn) {
        auto* fty = llvm::FunctionType::get(ctx.builder.getVoidTy(), {ptrTy, ptrTy, ptrTy}, false);
        throwFn = llvm::Function::Create(fty, llvm::Function::ExternalLinkage,
            "__cxa_throw", ctx.module());
    }

    // The object: the built-in exception class carries exactly one field
    // (`message`, a string) — see the registration in ast_compiler.cpp.
    llvm::Value* obj = llvm::ConstantPointerNull::get(ptrTy);
    auto it = ctx.classes.find(excClass);
    if (msgPtr && it != ctx.classes.end()) {
        if (auto* st = llvm::dyn_cast_or_null<llvm::StructType>(it->second.type)) {
            uint64_t sz = ctx.module().getDataLayout().getTypeAllocSize(st);
            // size_t, not a fixed i64: on wasm32 the rest of the code declares
            // `malloc(i32)` (ctx.getSizeType()), and a different width
            // makes the whole module invalid.
            llvm::Function* mallocFn = ctx.getFunction("malloc", ptrTy, {ctx.getSizeType()});
            obj = ctx.builder.CreateCall(mallocFn, {ctx.getSizeConst(sz)}, "exc_obj");
            ctx.builder.CreateStore(msgPtr,
                ctx.builder.CreateStructGEP(st, obj, 0, "exc_msg_slot"));
            if (codeVal && st->getNumElements() > 1 && st->getElementType(1)->isIntegerTy())
                ctx.builder.CreateStore(
                    ctx.builder.CreateIntCast(codeVal, st->getElementType(1), true),
                    ctx.builder.CreateStructGEP(st, obj, 1, "exc_code_slot"));
        }
    }

    // The same note that `throw` makes in aiml (ast_compiler_stmt.cpp): if nobody
    // catches, the top level reports the REASON instead of
    // „(no message)" (compiler.cpp jitRun).
    if (msgPtr)
        ctx.builder.CreateCall(ctx.getFunction("aiml_note_thrown_message",
                                               ctx.builder.getVoidTy(), {ptrTy}), {msgPtr});

    llvm::Value* excAlloc = ctx.builder.CreateCall(allocFn,
        {ctx.getSizeConst(ctx.getExcRecordSize())}, "exc_alloc");
    ctx.builder.CreateStore(obj, ctx.excRecordField(excAlloc, 0, "exc_obj_slot"));
    ctx.builder.CreateStore(llvm::ConstantPointerNull::get(ptrTy),
        ctx.excRecordField(excAlloc, 1, "exc_thunk_slot"));
    ctx.builder.CreateStore(ctx.builder.CreateGlobalStringPtr(excClass, "builtin_exc_tag"),
        ctx.excRecordField(excAlloc, 2, "exc_tag_slot"));
    llvm::Value* null = llvm::ConstantPointerNull::get(ptrTy);
    llvm::Constant* tinfo = ctx.getExcTypeInfo(excClass);

    llvm::BasicBlock* ud = AIML::AST::aimlUnwindDest(ctx, true);
    if (ud) {
        llvm::Function* fn = ctx.builder.GetInsertBlock()->getParent();
        llvm::BasicBlock* unreachBB = llvm::BasicBlock::Create(ctx.context, "throw_unreach", fn);
        ctx.builder.CreateInvoke(throwFn, unreachBB, ud, {excAlloc, tinfo, null});
        ctx.builder.SetInsertPoint(unreachBB);
    } else {
        ctx.builder.CreateCall(throwFn, {excAlloc, tinfo, null});
    }
    ctx.builder.CreateUnreachable();
}

// Like emitGuard, only with a reason.
static llvm::BasicBlock* emitGuardMsg(CompilerContext& ctx, llvm::Value* flag,
                                      const std::string& throwLabel,
                                      const std::string& continueLabel,
                                      const std::string& excClass,
                                      llvm::Value* msgPtr) {
    llvm::Function* fn = ctx.builder.GetInsertBlock()->getParent();
    llvm::Value* isSet = ctx.builder.CreateICmpNE(flag, ctx.builder.getInt8(0), throwLabel + "_cmp");
    llvm::BasicBlock* throwBB    = llvm::BasicBlock::Create(ctx.context, throwLabel, fn);
    llvm::BasicBlock* continueBB = llvm::BasicBlock::Create(ctx.context, continueLabel, fn);
    ctx.builder.CreateCondBr(isSet, throwBB, continueBB);
    ctx.builder.SetInsertPoint(throwBB);
    emitThrowMsg(ctx, excClass, msgPtr);
    ctx.builder.SetInsertPoint(continueBB);
    return continueBB;
}

// Like emitGuardMsg, only the throw additionally carries a code (ZimError, spec/33_2 §6.4).
static llvm::BasicBlock* emitGuardMsgCode(CompilerContext& ctx, llvm::Value* flag,
                                          const std::string& throwLabel,
                                          const std::string& continueLabel,
                                          const std::string& excClass,
                                          llvm::Value* msgPtr, llvm::Value* codeVal) {
    llvm::Function* fn = ctx.builder.GetInsertBlock()->getParent();
    llvm::Value* isSet = ctx.builder.CreateICmpNE(flag, ctx.builder.getInt8(0), throwLabel + "_cmp");
    llvm::BasicBlock* throwBB    = llvm::BasicBlock::Create(ctx.context, throwLabel, fn);
    llvm::BasicBlock* continueBB = llvm::BasicBlock::Create(ctx.context, continueLabel, fn);
    ctx.builder.CreateCondBr(isSet, throwBB, continueBB);
    ctx.builder.SetInsertPoint(throwBB);
    emitThrowMsg(ctx, excClass, msgPtr, codeVal);
    ctx.builder.SetInsertPoint(continueBB);
    return continueBB;
}

namespace AIML { namespace AST {
void aimlEmitThrowIf(CompilerContext& ctx, llvm::Value* cond, const std::string& excClass,
                     llvm::Value* msgPtr) {
    emitGuardMsg(ctx, ctx.builder.CreateZExt(cond, ctx.builder.getInt8Ty()), "guard_throw", "guard_ok",
                 excClass, msgPtr);
}
}}

// Emit a conditional check: if `flag` (i8) != 0, throw; else continue in `continueBB`.
// Returns the new insert-point BB (continueBB) with builder already set.
static llvm::BasicBlock* emitGuard(CompilerContext& ctx, llvm::Value* flag,
                                    const std::string& throwLabel,
                                    const std::string& continueLabel,
                                    const std::string& excClass) {
    llvm::Function* fn = ctx.builder.GetInsertBlock()->getParent();
    llvm::Value* isSet = ctx.builder.CreateICmpNE(flag, ctx.builder.getInt8(0), throwLabel + "_cmp");
    llvm::BasicBlock* throwBB    = llvm::BasicBlock::Create(ctx.context, throwLabel, fn);
    llvm::BasicBlock* continueBB = llvm::BasicBlock::Create(ctx.context, continueLabel, fn);
    ctx.builder.CreateCondBr(isSet, throwBB, continueBB);
    ctx.builder.SetInsertPoint(throwBB);
    emitThrow(ctx, excClass);
    ctx.builder.SetInsertPoint(continueBB);
    return continueBB;
}

// ---------------------------------------------------------------------------
// registerBuiltins
// ---------------------------------------------------------------------------
// spec/44_5 (AP1/AP5): resolve an RPC operand.  Model: value model → its storage; ptr<T>/ref<T>/
// shared<T> → the loaded object pointer; a refcount MODEL local holds a %M* → load it (a pointer slot
// is never treated as the object).  Event: the rpc_event variable's storage.
struct RpcModelArg {
    llvm::Value* ptr = nullptr; llvm::Value* nodes = nullptr; llvm::Value* n = nullptr;
    uint64_t size = 0; std::string tn;
};
static Symbol* rpcLookupSymbol(CompilerContext& ctx, const std::string& name) {
    std::string vname = (name == "<") ? std::string("this") : name;
    CompilerContext::Scope* sc = ctx.currentScope;
    while (sc) {
        auto it = sc->variables.find(vname);
        if (it != sc->variables.end()) return &it->second;
        sc = sc->parent;
    }
    auto it = ctx.symbol_table.find(vname);
    return it != ctx.symbol_table.end() ? &it->second : nullptr;
}
static llvm::Value* rpcSymbolStorage(CompilerContext& ctx, Symbol* sym) {
    return sym->alloca_inst ? static_cast<llvm::Value*>(sym->alloca_inst)
                            : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(sym->ir_alloca_inst));
}
static bool rpcModelArg(CompilerContext& ctx, AIML::AST::Expr* expr, RpcModelArg& out) {
    auto* vx = dynamic_cast<AIML::AST::VariableExpr*>(expr);
    if (!vx) return false;
    Symbol* sym = rpcLookupSymbol(ctx, vx->name);
    if (!sym) return false;
    std::string tn = sym->typeName;
    bool indirect = false;
    for (const char* w : {"ptr<", "ref<", "shared<"}) {
        size_t wl = strlen(w);
        if (tn.size() > wl && tn.compare(0, wl, w) == 0 && tn.back() == '>') {
            tn = tn.substr(wl, tn.size() - wl - 1); indirect = true; break;
        }
    }
    auto ci = ctx.classes.find(tn);
    if (ci == ctx.classes.end() || !ci->second.serNodesVar || !ci->second.type) return false;
    if (!indirect && ctx.isRefcountModel(tn)) indirect = true;
    llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
    llvm::Value* av = rpcSymbolStorage(ctx, sym);
    out.ptr   = indirect ? ctx.builder.CreateLoad(i8p, av, "rpc_obj") : ctx.builder.CreateBitCast(av, i8p);
    out.nodes = ci->second.serNodesVar;
    out.n     = ctx.builder.getInt32((uint32_t)ci->second.serNodesCount);
    out.size  = ctx.module().getDataLayout().getTypeAllocSize(ci->second.type);
    out.tn    = tn;
    return true;
}
static llvm::Value* rpcEventArg(CompilerContext& ctx, AIML::AST::Expr* expr) {
    auto* vx = dynamic_cast<AIML::AST::VariableExpr*>(expr);
    if (!vx) return nullptr;
    Symbol* sym = rpcLookupSymbol(ctx, vx->name);
    if (!sym || sym->typeName != "rpc_event") return nullptr;
    return ctx.builder.CreateBitCast(rpcSymbolStorage(ctx, sym), ctx.builder.getInt8Ty()->getPointerTo());
}

// spec/44_5 §7.1: hand the runtime the rpc table of a registered object's class (own + inherited
// methods): { i8* method, i8* args_nodes, i32 n_nodes, i64 args_size, i8* thunk } per entry.
static void emitRpcTableRegistration(CompilerContext& ctx, llvm::Value* sess, llvm::Value* objPtr,
                                     const std::string& className) {
    std::vector<const CompilerContext::RpcMethodEntry*> ents;
    std::set<std::string> names;
    std::string c = className;
    for (const char* w : {"ptr<", "ref<", "shared<"}) {
        size_t wl = strlen(w);
        if (c.size() > wl && c.compare(0, wl, w) == 0 && c.back() == '>') { c = c.substr(wl, c.size() - wl - 1); break; }
    }
    for (int guard = 0; !c.empty() && guard < 32; ++guard) {
        auto it = ctx.rpcMethodsByClass.find(c);
        if (it != ctx.rpcMethodsByClass.end())
            for (auto& e : it->second) if (names.insert(e.method).second) ents.push_back(&e);
        auto ci = ctx.classes.find(c);
        if (ci == ctx.classes.end()) break;
        c = ci->second.parentName;
    }
    if (ents.empty()) return;
    llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
    llvm::Type* i32 = ctx.builder.getInt32Ty();
    llvm::Type* i64 = ctx.builder.getInt64Ty();
    llvm::StructType* entTy = llvm::StructType::get(ctx.context, {i8p, i8p, i32, i64, i8p});
    std::vector<llvm::Constant*> rows;
    for (auto* e : ents) {
        auto ai = ctx.classes.find(e->argsModel);
        if (ai == ctx.classes.end() || !ai->second.serNodesVar || !ai->second.type) {
            LOG_ERROR(ctx.fmtLoc() + "rpc table: argument model '" + e->argsModel + "' has no serialization descriptor");
            continue;
        }
        llvm::Function* th = ctx.module().getFunction(e->thunk);
        if (!th)
            th = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, i8p}, false),
                                        llvm::Function::ExternalLinkage, e->thunk, ctx.module());
        uint64_t sz = ctx.module().getDataLayout().getTypeAllocSize(ai->second.type);
        rows.push_back(llvm::ConstantStruct::get(entTy, {
            ctx.builder.CreateGlobalStringPtr(e->method, "rpc_m"),
            llvm::cast<llvm::Constant>(ai->second.serNodesVar),
            llvm::ConstantInt::get(i32, (uint64_t)ai->second.serNodesCount),
            llvm::ConstantInt::get(i64, sz),
            th }));
    }
    if (rows.empty()) return;
    auto* arrTy = llvm::ArrayType::get(entTy, rows.size());
    auto* gv = new llvm::GlobalVariable(ctx.module(), arrTy, true, llvm::GlobalValue::PrivateLinkage,
                                        llvm::ConstantArray::get(arrTy, rows), "__aiml_rpc_table_" + className);
    ctx.builder.CreateCall(
        ctx.getFunction("aiml_mirror_session_set_rpc_table", ctx.builder.getVoidTy(), {i8p, i8p, i8p, i32}),
        {sess, objPtr, ctx.builder.CreateBitCast(gv, i8p), ctx.builder.getInt32((uint32_t)rows.size())});
}

// ptr<T>/ref<T>/shared<T> variables hold the object pointer — mirror operations act on the object.
static bool mirrorIsPtrLike(const std::string& t) {
    return !t.empty() && t.back() == '>' && (t.rfind("ptr<", 0) == 0 || t.rfind("ref<", 0) == 0 || t.rfind("shared<", 0) == 0);
}
static std::string mirrorInnerType(const std::string& t) {
    for (const char* w : {"ptr<", "ref<", "shared<"}) {
        size_t wl = strlen(w);
        if (t.size() > wl && t.compare(0, wl, w) == 0 && t.back() == '>') return t.substr(wl, t.size() - wl - 1);
    }
    return t;
}

// spec/44_5 §4/§10: a registered user type must have a serializer descriptor (no raw fallback).
static bool mirrorSyncableType(CompilerContext& ctx, const std::string& tn) {
    auto it = ctx.classes.find(tn);
    if (tn.empty() || it == ctx.classes.end() || it->second.isBuiltin || it->second.serNodesVar) return true;
    LOG_ERROR(ctx.fmtLoc() + "mirror_session: '" + tn + "' has no serializer descriptor and cannot be synchronized");
    return false;
}

// A buffer argument arrives as a loaded {size, cap, rIdx, wIdx, data} struct (local) or as a
// pointer to it (member / handle): read data pointer and size either way.
static void bufferArgDataLen(CompilerContext& ctx, llvm::Value* v, llvm::Value*& data, llvm::Value*& len) {
    llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
    if (v->getType()->isStructTy()) {
        data = ctx.builder.CreateExtractValue(v, {4}, "buf_arg_data");
        len  = ctx.builder.CreateExtractValue(v, {0}, "buf_arg_len");
    } else {
        llvm::Type* bufTy = ctx.getLLVMType("buffer");
        data = ctx.builder.CreateLoad(i8p, ctx.builder.CreateStructGEP(bufTy, v, 4), "buf_arg_data");
        len  = ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), ctx.builder.CreateStructGEP(bufTy, v, 0), "buf_arg_len");
    }
}

// A string the runtime returned as a new managed reference: tracked as a
// statement temp under lifetime drops, so it is freed after the statement unless
// a variable or a container adopts it (e.g. used directly as a call argument).
static IRValue* ownedStringResult(CompilerContext& ctx, llvm::Value* v) {
    if (ctx.lifetimeDropsEnabled && !ctx.stmtTempStack.empty())
        AIML::AST::aimlTrackStringTemp(ctx, v);
    return ctx.ir_gen->wrap(v);
}

// A fresh list<string> (memV3 handle, else a stack list) for a *_fill runtime call.
static llvm::Value* freshStringList(CompilerContext& ctx, const char* name) {
    llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
    llvm::Value* lst = ctx.memV3
        ? static_cast<llvm::Value*>(ctx.builder.CreateCall(ctx.getFunction("aiml_list_handle_new", i8p, {}), {}, name))
        : static_cast<llvm::Value*>(ctx.createEntryAlloca(ListImpl::getListStructType(ctx), name));
    ListImpl::initList(ctx, lst, i8p, 8);
    return lst;
}

// Argument `i` as a value, or "" when the call has fewer arguments.
static llvm::Value* argOrEmpty(CompilerContext& ctx, const std::vector<IRValue*>& args, size_t i) {
    return args.size() > i ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]))
                           : static_cast<llvm::Value*>(ctx.builder.CreateGlobalStringPtr(""));
}

// The address of a buffer argument, for runtime calls that read or fill the
// buffer in place (net.recv sets size/writeIndex).  A local arrives as a load of
// its storage: the load's own address is the variable.  Anything else (a
// computed buffer value) is spilled to a temporary — fine for reads only.
static llvm::Value* bufferArgAddress(CompilerContext& ctx, llvm::Value* v) {
    if (v->getType()->isPointerTy()) return v;
    if (auto* load = llvm::dyn_cast<llvm::LoadInst>(v)) return load->getPointerOperand();
    auto* tmp = ctx.createEntryAlloca(ctx.getLLVMType("buffer"), "buf_arg_tmp");
    ctx.builder.CreateStore(v, tmp);
    return tmp;
}

// spec/43_5 call lowering. sig chars: i=i32 (bool too), f=f32, p=pointer/string/handle,
// b=buffer (address). ret: v=void, i=i32, p=pointer, B=owned heap buffer -> buffer value.
static llvm::Value* gl43Arg(CompilerContext& ctx, llvm::Value* v, char k) {
    auto* i32 = ctx.builder.getInt32Ty();
    if (k == 'i') {
        if (v->getType()->isIntegerTy(1) || v->getType()->isIntegerTy(8)) return ctx.builder.CreateZExt(v, i32);
        return ctx.builder.CreateSExtOrTrunc(v, i32);
    }
    if (k == 'f') {
        if (v->getType()->isIntegerTy()) return ctx.builder.CreateSIToFP(v, ctx.builder.getFloatTy());
        return v->getType()->isFloatTy() ? v : ctx.builder.CreateFPCast(v, ctx.builder.getFloatTy());
    }
    if (k == 'b' && !v->getType()->isPointerTy()) {
        auto* a = ctx.createEntryAlloca(ctx.getLLVMType("buffer"), "gl43_buf_in");
        ctx.builder.CreateStore(v, a);
        return a;
    }
    return v;
}

static llvm::Type* gl43Ty(CompilerContext& ctx, char k) {
    if (k == 'i' || k == 'z') return ctx.builder.getInt32Ty();
    if (k == 'l') return ctx.builder.getInt64Ty();
    if (k == 'f') return ctx.builder.getFloatTy();
    if (k == 'v') return ctx.builder.getVoidTy();
    return llvm::PointerType::get(ctx.context, 0);
}

static llvm::Value* gl43Default(CompilerContext& ctx, char k) {
    if (k == 'i') return ctx.builder.getInt32(0);
    if (k == 'f') return llvm::ConstantFP::get(ctx.builder.getFloatTy(), 0.0);
    return llvm::ConstantPointerNull::get(llvm::PointerType::get(ctx.context, 0));
}

static IRValue* gl43Emit(CompilerContext& ctx, const char* cName, const std::string& sig, char ret,
                         std::vector<llvm::Value*> vals) {
    std::vector<llvm::Type*> tys;
    for (char k : sig) tys.push_back(gl43Ty(ctx, k));
    llvm::Value* r = ctx.builder.CreateCall(ctx.getFunction(cName, gl43Ty(ctx, ret == 'B' ? 'p' : ret), tys), vals);
    if (ret == 'v') return nullptr;
    if (ret == 'z') return ctx.ir_gen->wrap(ctx.builder.CreateICmpNE(r, ctx.builder.getInt32(0), "gl43_bool"));
    if (ret == 'B') {
        auto* p   = llvm::PointerType::get(ctx.context, 0);
        auto* bt  = ctx.getLLVMType("buffer");
        uint64_t sz = ctx.module().getDataLayout().getTypeAllocSize(bt);
        llvm::Value* out = ctx.memV3
            ? static_cast<llvm::Value*>(ctx.builder.CreateCall(ctx.getFunction("aiml_buffer_handle_new", p, {}), {}, "gl43_buf_h"))
            : static_cast<llvm::Value*>(ctx.createEntryAlloca(bt, "gl43_buf"));
        ctx.builder.CreateCall(ctx.getFunction("memcpy", p, {p, p, ctx.getSizeType()}),
            {out, r, ctx.getSizeConst(sz)});
        ctx.builder.CreateCall(ctx.getFunction("free", ctx.builder.getVoidTy(), {p}), {r});
        return ctx.ir_gen->wrap(out);
    }
    return ctx.ir_gen->wrap(r);
}

static bool glVfxCall(const char* name) {
    return std::string(name) == "aiml_gl_buffer_reserve_bytes"
        || std::string(name) == "aiml_gl_buffer_update_range"
        || std::string(name) == "aiml_gl_vao_attrib_divisor"
        || std::string(name) == "aiml_gl_draw_elements_instanced";
}
static bool glVfxArg(CompilerContext& ctx, llvm::Value* value, char kind, const char* name) {
    bool ok = value && ((kind == 'i' && value->getType()->isIntegerTy(32))
        || (kind == 'p' && value->getType()->isPointerTy())
        || (kind == 'b' && (value->getType()->isPointerTy() || value->getType() == ctx.getLLVMType("buffer"))));
    if (!ok) LOG_ERROR(ctx.fmtLoc() + std::string(name) + ": invalid argument type (expected "
        + (kind == 'i' ? "i32" : kind == 'b' ? "buffer<u8>" : "gl_vao") + ")");
    return ok;
}

static CompilerContext::FuncHandler gl43Fn(const char* cName, std::string sig, char ret) {
    return [cName, sig, ret](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
        if (glVfxCall(cName) && args.size() != sig.size()) {
            LOG_ERROR(ctx.fmtLoc() + std::string(cName) + ": wrong argument count"); return nullptr;
        }
        if (glVfxCall(cName)) for (size_t i = 0; i < args.size(); ++i)
            if (!glVfxArg(ctx, args[i] ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])) : nullptr, sig[i], cName)) return nullptr;
        std::vector<llvm::Value*> vals;
        for (size_t i = 0; i < sig.size(); ++i)
            vals.push_back(i < args.size()
                ? gl43Arg(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])), sig[i])
                : gl43Default(ctx, sig[i]));
        return gl43Emit(ctx, cName, sig, ret, vals);
    };
}

// Method: the receiver is the leading 'p' of sig.
static CompilerContext::MethodHandler gl43Method(const char* cName, std::string sig, char ret) {
    return [cName, sig, ret](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr* call,
                             std::function<AIML::IRValue*(AIML::AST::Expr*)> evalExpr) -> AIML::IRValue* {
        if (glVfxCall(cName) && (!call || call->args.size() != sig.size() - 1)) {
            LOG_ERROR(ctx.fmtLoc() + std::string(cName) + ": wrong argument count"); return nullptr;
        }
        std::vector<llvm::Value*> vals{base};
        for (size_t i = 1; i < sig.size(); ++i) {
            IRValue* a = (call && call->args.size() >= i) ? evalExpr(call->args[i - 1].get()) : nullptr;
            if (glVfxCall(cName) && !glVfxArg(ctx, a ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a)) : nullptr, sig[i], cName)) return nullptr;
            vals.push_back(a ? gl43Arg(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a)), sig[i])
                             : gl43Default(ctx, sig[i]));
        }
        return gl43Emit(ctx, cName, sig, ret, vals);
    };
}

void registerBuiltins(CompilerContext& ctx) {
    // spec/61 §8: builtins that hand back an OWNED string (aiml_str_dup) — the receiving local drops it.
    for (const char* fn : {"text_value", "selection"}) {
        ctx.ownsReturnFns.insert(fn);
        ctx.ownsReturnFnTypes[fn].insert("string");
        ctx.stringProducerFns.insert(fn);
    }
    ctx.enums["yaml"] = {{"null", 0}, {"scalar", 1}, {"sequence", 2}, {"mapping", 3}};

    // -----------------------------------------------------------------------
    // mirror.RpcHandler builtin interface
    //
    // Stand-in for the very common pattern in mirror-based servers:
    //   - class:
    //       name: LoginHandler
    //       implements: [mirror.RpcHandler]
    //       models:     [LoginReq]
    //       methods:
    //         - def:
    //             name: run
    //             args: [[rpc, rpc_event], [session, mirror_session]]
    //             body: [...]
    //
    // The interface contract is `run(rpc_event, mirror_session) -> void`.
    // A user-defined `RpcDispatcher` class then holds a
    // `map<string:ptr<mirror.RpcHandler>>` and dispatches per `rpc.method`.
    //
    // Registering it here saves every project from re-defining the same
    // interface block.  The vtable signature-check + missing-method
    // hard-fail (ast_compiler.cpp visitTopLevel) work unchanged because
    // they look up `cls->interfaces[i]` in `ctx.interfaces` by name.
    // -----------------------------------------------------------------------
    {
        CompilerContext::InterfaceInfo iface;
        iface.name = "mirror.RpcHandler";
        CompilerContext::InterfaceMethod run;
        run.name = "run";
        run.returnType = "void";
        run.args = {{"rpc", "rpc_event"}, {"session", "mirror_session"}};
        iface.methods.push_back(std::move(run));
        ctx.interfaces["mirror.RpcHandler"] = std::move(iface);
    }

    // -----------------------------------------------------------------------
    // VecResult class — real LLVM struct {i8*, f32} so that r.id / r.score
    // work via resolveProperty's GEP path. Must be registered before db namespace.
    // -----------------------------------------------------------------------
    {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::StructType* vrTy = llvm::StructType::getTypeByName(ctx.context, "VecResult");
        if (!vrTy) {
            vrTy = llvm::StructType::create(ctx.context,
                {i8p, ctx.builder.getFloatTy()}, "VecResult");
        }
        CompilerContext::ClassInfo vrInfo;
        vrInfo.name      = "VecResult";
        vrInfo.type      = vrTy;
        vrInfo.isBuiltin = false;

        CompilerContext::StructMember idMember;
        idMember.index    = 0;
        idMember.type     = i8p;
        idMember.typeName = "string";

        CompilerContext::StructMember scoreMember;
        scoreMember.index    = 1;
        scoreMember.type     = ctx.builder.getFloatTy();
        scoreMember.typeName = "f32";

        vrInfo.members["id"]    = idMember;
        vrInfo.members["score"] = scoreMember;
        ctx.classes["VecResult"] = std::move(vrInfo);
    }

    // -----------------------------------------------------------------------
    // JsonStyle builtin class for sys.serialize formatting
    // Layout: { i32 pretty, ptr indent, ptr newline, i32 bare, i32 skip_empty }
    // -----------------------------------------------------------------------
    {
        llvm::Type* i32 = ctx.builder.getInt32Ty();
        llvm::Type* p   = llvm::PointerType::get(ctx.context, 0);
        llvm::StructType* jsTy = llvm::StructType::getTypeByName(ctx.context, "JsonStyle");
        if (!jsTy) {
            jsTy = llvm::StructType::create(ctx.context, {i32, p, p, i32, i32}, "JsonStyle");
        }
        CompilerContext::ClassInfo jsInfo;
        jsInfo.name = "JsonStyle"; jsInfo.type = jsTy; jsInfo.isBuiltin = false;
        auto mk = [](llvm::Type* t, int idx, const std::string& tn) {
            CompilerContext::StructMember m; m.type = t; m.index = idx; m.typeName = tn; return m;
        };
        jsInfo.members["pretty"]  = mk(i32, 0, "i32");
        jsInfo.members["indent"]  = mk(p,   1, "string");
        jsInfo.members["newline"] = mk(p,   2, "string");
        jsInfo.members["bare"]    = mk(i32, 3, "i32");   // spec/29 §5.2a: without __type
        jsInfo.members["skip_empty"] = mk(i32, 4, "i32");   // spec/29 §5.2c: Leeres weglassen
        // spec/61 §7/§6.1: JsonStyle was already registered (context.h) and registerTypes built its
        // heap-field RELEASE metadata (hasHeapFields + serNodes) before compile→registerBuiltins runs.
        // Overwriting the ClassInfo wholesale dropped that metadata (new entry defaults
        // hasHeapFields=false, serNodesVar=null) → the model scope-drop for its owned `indent`/
        // `newline` string fields was never registered → 21 B leaked per `set style.indent/.newline`
        // (leak_probes/round6 §2). Preserve the built descriptors when re-registering.
        if (auto ex = ctx.classes.find("JsonStyle"); ex != ctx.classes.end()) {
            jsInfo.hasHeapFields = ex->second.hasHeapFields;
            jsInfo.serNodesVar   = ex->second.serNodesVar;
            jsInfo.serNodesCount = ex->second.serNodesCount;
        }
        ctx.classes["JsonStyle"] = std::move(jsInfo);
    }

    // -----------------------------------------------------------------------
    // zip_entry — archive entry struct for compress.zip_entries
    // Layout: { ptr name, i64 size, i64 compressed_size, i32 is_dir, i32 crc32, i32 method, i32 _pad }
    // -----------------------------------------------------------------------
    {
        llvm::Type* p   = llvm::PointerType::get(ctx.context, 0);
        llvm::Type* i64 = ctx.builder.getInt64Ty();
        llvm::Type* sz  = ctx.getSizeType();
        llvm::Type* i32 = ctx.builder.getInt32Ty();
        llvm::StructType* zeTy = llvm::StructType::getTypeByName(ctx.context, "zip_entry");
        if (!zeTy) zeTy = llvm::StructType::create(ctx.context, {p, i64, i64, i32, i32, i32, i32}, "zip_entry");

        CompilerContext::ClassInfo zi;
        zi.name = "zip_entry"; zi.type = zeTy; zi.isBuiltin = false;
        auto mk = [](llvm::Type* t, int idx, const std::string& tn) {
            CompilerContext::StructMember m; m.type = t; m.index = idx; m.typeName = tn; return m;
        };
        zi.members["name"]            = mk(p,   0, "string");
        zi.members["size"]            = mk(i64, 1, "i64");
        zi.members["compressed_size"] = mk(i64, 2, "i64");
        zi.members["is_dir"]          = mk(i32, 3, "i32");
        zi.members["crc32"]           = mk(i32, 4, "i32");
        zi.members["method"]          = mk(i32, 5, "i32");
        if (auto ex = ctx.classes.find("zip_entry"); ex != ctx.classes.end()) {   // keep release descriptor (context.h)
            zi.hasHeapFields = ex->second.hasHeapFields;
            zi.serNodesVar   = ex->second.serNodesVar;
            zi.serNodesCount = ex->second.serNodesCount;
        }
        ctx.classes["zip_entry"] = std::move(zi);
    }

    // -----------------------------------------------------------------------
    // tar_entry — archive entry struct for compress.tar_entries
    // Layout: { ptr name, i64 size, i32 is_dir, i32 mode }
    // -----------------------------------------------------------------------
    {
        llvm::Type* p   = llvm::PointerType::get(ctx.context, 0);
        llvm::Type* i64 = ctx.builder.getInt64Ty();
        llvm::Type* sz  = ctx.getSizeType();
        llvm::Type* i32 = ctx.builder.getInt32Ty();
        llvm::StructType* teTy = llvm::StructType::getTypeByName(ctx.context, "tar_entry");
        if (!teTy) teTy = llvm::StructType::create(ctx.context, {p, i64, i32, i32}, "tar_entry");

        CompilerContext::ClassInfo ti;
        ti.name = "tar_entry"; ti.type = teTy; ti.isBuiltin = false;
        auto mk = [](llvm::Type* t, int idx, const std::string& tn) {
            CompilerContext::StructMember m; m.type = t; m.index = idx; m.typeName = tn; return m;
        };
        ti.members["name"]   = mk(p,   0, "string");
        ti.members["size"]   = mk(i64, 1, "i64");
        ti.members["is_dir"] = mk(i32, 2, "i32");
        ti.members["mode"]   = mk(i32, 3, "i32");
        if (auto ex = ctx.classes.find("tar_entry"); ex != ctx.classes.end()) {   // keep release descriptor (context.h)
            ti.hasHeapFields = ex->second.hasHeapFields;
            ti.serNodesVar   = ex->second.serNodesVar;
            ti.serNodesCount = ex->second.serNodesCount;
        }
        ctx.classes["tar_entry"] = std::move(ti);
    }

    // -----------------------------------------------------------------------
    // zim_entry — one entry of a ZIM archive (spec/33_2 §6.2)
    // Layout: { i64 index, i64 size, i64 position, i8 found, i8 is_redirect }
    //
    // SCALARS ONLY, deliberately.  §6.2 calls the entry "a value model (copyable; it holds
    // only an index into the open archive)", and that is what makes it one: with no string
    // field there is nothing to own, so a `list<zim_entry>` of a million rows copies freely
    // and cannot leak, and a scan can hand back a flat buffer.  The four strings (path,
    // title, mimetype, redirect_path) are read through the HANDLE instead — zim.path(entry)
    // and friends — because only the archive can answer them anyway.
    // -----------------------------------------------------------------------
    {
        llvm::Type* i64 = ctx.builder.getInt64Ty();
        llvm::Type* i8  = ctx.builder.getInt8Ty();
        llvm::StructType* zeTy = llvm::StructType::getTypeByName(ctx.context, "zim_entry");
        if (!zeTy) zeTy = llvm::StructType::create(ctx.context, {i64, i64, i64, i8, i8}, "zim_entry");

        CompilerContext::ClassInfo zi;
        zi.name = "zim_entry"; zi.type = zeTy; zi.isBuiltin = false;
        auto mk = [](llvm::Type* t, int idx, const std::string& tn) {
            CompilerContext::StructMember m; m.type = t; m.index = idx; m.typeName = tn; return m;
        };
        zi.members["index"]       = mk(i64, 0, "i64");
        zi.members["size"]        = mk(i64, 1, "i64");
        zi.members["position"]    = mk(i64, 2, "i64");
        zi.members["found"]       = mk(i8,  3, "bool");
        zi.members["is_redirect"] = mk(i8,  4, "bool");
        if (auto ex = ctx.classes.find("zim_entry"); ex != ctx.classes.end()) {
            zi.hasHeapFields = ex->second.hasHeapFields;
            zi.serNodesVar   = ex->second.serNodesVar;
            zi.serNodesCount = ex->second.serNodesCount;
        }
        ctx.classes["zim_entry"] = std::move(zi);
    }

    // -----------------------------------------------------------------------
    // cascade namespace  —  unit-testable helpers for cascade wire format
    // -----------------------------------------------------------------------
    {
        auto& cas = ctx.namespaces["cascade"];
        // cascade.persist_row(outer: list<list<i32>>, bound_index: i32,
        //                     tag: i32,               values: list<i32>)
        // Grows the row at bound_index to tag + n_values slots, then writes:
        //     row[0]      = tag
        //     row[1..N]   = values[0..N-1]
        // Standalone from the widget runtime so it can be exercised headless.
        cas.builtinFuncs["persist_row"] = [](CompilerContext& ctx,
                                             const std::vector<IRValue*>& args) -> IRValue* {
            auto* vd  = ctx.builder.getVoidTy();
            auto* i32 = ctx.builder.getInt32Ty();
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            if (args.size() < 4) return ctx.ir_gen->getInt32(0);
            llvm::Value* outer = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* idx   = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            llvm::Value* tag   = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]));
            llvm::Value* vals  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[3]));
            /* args[3] may arrive as either a struct-by-value (loaded from
             * the local list<i32> alloca) or already a pointer.  If we got
             * a struct, spill it to a fresh alloca first so we can GEP on
             * a pointer.  outer (args[0]) is a list<list<i32>> which reads
             * as a struct pointer; also normalise the same way. */
            llvm::StructType* listTy = ListImpl::getListStructType(ctx);
            auto normalise = [&](llvm::Value* v, const char* name) -> llvm::Value* {
                if (v->getType()->isPointerTy()) return v;
                llvm::IRBuilder<> tmp(&ctx.builder.GetInsertBlock()->getParent()->getEntryBlock(),
                                       ctx.builder.GetInsertBlock()->getParent()->getEntryBlock().begin());
                llvm::AllocaInst* al = tmp.CreateAlloca(v->getType(), nullptr, name);
                ctx.builder.CreateStore(v, al);
                return al;
            };
            llvm::Value* outerP = normalise(outer, "outer_spill");
            llvm::Value* valsP  = normalise(vals,  "vals_spill");
            llvm::Value* sizeGep = ctx.builder.CreateStructGEP(listTy, valsP, 0, "vals_size_gep");
            llvm::Value* nVals   = ctx.builder.CreateLoad(i32, sizeGep, "vals_size");
            llvm::Value* dataGep = ctx.builder.CreateStructGEP(listTy, valsP, 2, "vals_data_gep");
            llvm::Value* dataP   = ctx.builder.CreateLoad(i8p,  dataGep, "vals_data");
            ctx.builder.CreateCall(
                ctx.getFunction("aiml_cascade_persist_row", vd,
                                {i8p, i32, i32, i8p, i32}),
                {outerP, idx, tag, dataP, nVals});
            return ctx.ir_gen->getInt32(0);
        };

        // cascade.validate(name: string, row: list<i32>) → i32
        //   -1 valid | >=0 first bad index | -2 unknown cascade.
        // cascade.explain(name: string, row: list<i32>) → string  (reason / "").
        // Both extract row's (data_ptr, size) from the list struct (size@0,
        // data@2) and forward to the aiml_cascade_* runtime.
        auto cascadeCheck = [](CompilerContext& ctx, const std::vector<IRValue*>& args,
                               const char* fnName, llvm::Type* retTy) -> llvm::Value* {
            auto* i32 = ctx.builder.getInt32Ty();
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));   // string → i8p
            llvm::Value* row  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));   // list<i32>
            llvm::StructType* listTy = ListImpl::getListStructType(ctx);
            llvm::Value* rowP = row;
            if (!row->getType()->isPointerTy()) {
                llvm::IRBuilder<> tmp(&ctx.builder.GetInsertBlock()->getParent()->getEntryBlock(),
                                       ctx.builder.GetInsertBlock()->getParent()->getEntryBlock().begin());
                llvm::AllocaInst* al = tmp.CreateAlloca(row->getType(), nullptr, "row_spill");
                ctx.builder.CreateStore(row, al);
                rowP = al;
            }
            llvm::Value* sizeGep = ctx.builder.CreateStructGEP(listTy, rowP, 0, "row_size_gep");
            llvm::Value* len     = ctx.builder.CreateLoad(i32, sizeGep, "row_len");
            llvm::Value* dataGep = ctx.builder.CreateStructGEP(listTy, rowP, 2, "row_data_gep");
            llvm::Value* dataP   = ctx.builder.CreateLoad(i8p, dataGep, "row_data");
            return ctx.builder.CreateCall(ctx.getFunction(fnName, retTy, {i8p, i8p, i32}),
                                          {name, dataP, len});
        };

        cas.builtinFuncs["validate"] = [cascadeCheck](CompilerContext& ctx,
                                        const std::vector<IRValue*>& args) -> IRValue* {
            if (args.size() < 2) return ctx.ir_gen->getInt32(-2);
            return ctx.ir_gen->wrap(cascadeCheck(ctx, args, "aiml_cascade_validate",
                                                 ctx.builder.getInt32Ty()));
        };
        cas.builtinFuncs["explain"] = [cascadeCheck](CompilerContext& ctx,
                                       const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            if (args.size() < 2) return ctx.ir_gen->wrap(ctx.builder.CreateGlobalStringPtr(""));
            return ctx.ir_gen->wrap(cascadeCheck(ctx, args, "aiml_cascade_explain", i8p));
        };

        // cascade.field_indices(name: string, row: list<i32>, field_type: string)
        //     → list<i32>
        //   Row indices of every payload field of type `field_type` in the node
        //   `row` selects (e.g. "TargetType" in an Effect row — a type may recur).
        //   Empty list = the field does not occur in that node.  Allocates a fresh
        //   list<i32>, fills it via the runtime, returns it (io.list_dir pattern).
        cas.builtinFuncs["field_indices"] = [](CompilerContext& ctx,
                                              const std::vector<IRValue*>& args) -> IRValue* {
            auto* i32   = ctx.builder.getInt32Ty();
            auto* i8p   = ctx.builder.getInt8Ty()->getPointerTo();
            auto* ptrTy = llvm::PointerType::get(ctx.context, 0);
            llvm::StructType* listTy = ListImpl::getListStructType(ctx);
            // Fresh list<i32> for the result. spec/61 §5 (memV3): a `list<i32>` binding is a
            // refcounted HEAP HANDLE freed via aiml_list_handle_release — returning a pointer to a
            // STACK alloca made the caller's local own stack memory, so its scope-drop released a
            // stack pointer (garbage rc → crash: do_deal_damage's eff_pos → field_indices, the
            // test_creature_damage/effect rc134 cluster). Mirror the `.split` fix.
            llvm::Value* outAlloca;
            if (ctx.memV3) {
                outAlloca = ctx.builder.CreateCall(
                    ctx.getFunction("aiml_list_handle_new", i8p, {}), {}, "casc_fidx_h");
                ListImpl::initList(ctx, outAlloca, i32, 4);
            } else {
                outAlloca = ctx.createEntryAlloca(listTy, "casc_fidx_out");
                ListImpl::initList(ctx, outAlloca, i32, 4);
            }
            if (args.size() < 3) return ctx.ir_gen->wrap(outAlloca);
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* row  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            llvm::Value* ftyp = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]));
            // Extract row's (data_ptr, size).
            llvm::Value* rowP = row;
            if (!row->getType()->isPointerTy()) {
                llvm::IRBuilder<> tmp(&ctx.builder.GetInsertBlock()->getParent()->getEntryBlock(),
                                       ctx.builder.GetInsertBlock()->getParent()->getEntryBlock().begin());
                llvm::AllocaInst* al = tmp.CreateAlloca(row->getType(), nullptr, "row_spill");
                ctx.builder.CreateStore(row, al);
                rowP = al;
            }
            llvm::Value* sizeGep = ctx.builder.CreateStructGEP(listTy, rowP, 0, "row_size_gep");
            llvm::Value* len     = ctx.builder.CreateLoad(i32, sizeGep, "row_len");
            llvm::Value* dataGep = ctx.builder.CreateStructGEP(listTy, rowP, 2, "row_data_gep");
            llvm::Value* dataP   = ctx.builder.CreateLoad(i8p, dataGep, "row_data");
            ctx.builder.CreateCall(
                ctx.getFunction("aiml_cascade_field_indices", i32, {i8p, i8p, i32, i8p, ptrTy}),
                {name, dataP, len, ftyp, outAlloca});
            return ctx.ir_gen->wrap(outAlloca);
        };

        // cascade.count(name: string, start_op?: i32, end_op?: i32) → i64
        //   Total number of enumerable rows: opcodes × enum-members × nested
        //   sub-opcodes (value/alias fields = 1 representative).  Optional
        //   start_op/end_op bound the TOP-LEVEL opcode ids (defaults = all).
        //   Saturates at i64 max instead of wrapping.
        cas.builtinFuncs["count"] = [](CompilerContext& ctx,
                                       const std::vector<IRValue*>& args) -> IRValue* {
            auto* i32 = ctx.builder.getInt32Ty();
            auto* i64 = ctx.builder.getInt64Ty();
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            if (args.empty()) return ctx.ir_gen->wrap(ctx.builder.getInt64(0));
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            auto opArg = [&](size_t i, int32_t dflt) -> llvm::Value* {
                if (args.size() <= i) return ctx.builder.getInt32(dflt);
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]));
                if (v->getType() != i32) v = ctx.builder.CreateSExtOrTrunc(v, i32);
                return v;
            };
            llvm::Value* lo = opArg(1, (-2147483647 - 1));
            llvm::Value* hi = opArg(2, 2147483647);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_cascade_row_count", i64, {i8p, i32, i32}),
                {name, lo, hi}));
        };

        // cascade.row_at(name: string, k: i64, start_op?: i32, end_op?: i32)
        //     → list<i32>   (the k-th enumerated row; empty if k out of range).
        //   Mixed-radix decode — O(depth), no materialisation.  Loop k in
        //   [0, cascade.count(...)) to stream every row.
        cas.builtinFuncs["row_at"] = [](CompilerContext& ctx,
                                        const std::vector<IRValue*>& args) -> IRValue* {
            auto* i32   = ctx.builder.getInt32Ty();
            auto* i64   = ctx.builder.getInt64Ty();
            auto* i8p   = ctx.builder.getInt8Ty()->getPointerTo();
            auto* ptrTy = llvm::PointerType::get(ctx.context, 0);
            llvm::StructType* listTy = ListImpl::getListStructType(ctx);
            // spec/61 §5 (memV3): return a HEAP handle, not a stack alloca (see field_indices).
            llvm::Value* outAlloca;
            if (ctx.memV3) {
                outAlloca = ctx.builder.CreateCall(ctx.getFunction("aiml_list_handle_new", i8p, {}), {}, "casc_row_h");
                ListImpl::initList(ctx, outAlloca, i32, 8);
            } else {
                outAlloca = ctx.createEntryAlloca(listTy, "casc_row_out");
                ListImpl::initList(ctx, outAlloca, i32, 8);
            }
            if (args.size() < 2) return ctx.ir_gen->wrap(outAlloca);
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* k    = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            if (k->getType() != i64) k = ctx.builder.CreateSExtOrTrunc(k, i64);
            auto opArg = [&](size_t i, int32_t dflt) -> llvm::Value* {
                if (args.size() <= i) return ctx.builder.getInt32(dflt);
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]));
                if (v->getType() != i32) v = ctx.builder.CreateSExtOrTrunc(v, i32);
                return v;
            };
            llvm::Value* lo = opArg(2, (-2147483647 - 1));
            llvm::Value* hi = opArg(3, 2147483647);
            ctx.builder.CreateCall(
                ctx.getFunction("aiml_cascade_row_at", i32, {i8p, i64, i32, i32, ptrTy}),
                {name, k, lo, hi, outAlloca});
            return ctx.ir_gen->wrap(outAlloca);
        };

        // cascade.type_range(type: string) → list<i32>
        //   Possible values of a field type: enum→member values, cascade→sub-node
        //   IDs, value/alias→empty.  Serialisable (sys.serialize).
        cas.builtinFuncs["type_range"] = [](CompilerContext& ctx,
                                            const std::vector<IRValue*>& args) -> IRValue* {
            auto* i32   = ctx.builder.getInt32Ty();
            auto* i8p   = ctx.builder.getInt8Ty()->getPointerTo();
            auto* ptrTy = llvm::PointerType::get(ctx.context, 0);
            llvm::StructType* listTy = ListImpl::getListStructType(ctx);
            // spec/61 §5 (memV3): return a HEAP handle, not a stack alloca (see field_indices).
            llvm::Value* out;
            if (ctx.memV3) {
                out = ctx.builder.CreateCall(ctx.getFunction("aiml_list_handle_new", i8p, {}), {}, "casc_range_h");
                ListImpl::initList(ctx, out, i32, 8);
            } else {
                out = ctx.createEntryAlloca(listTy, "casc_range_out");
                ListImpl::initList(ctx, out, i32, 8);
            }
            if (args.empty()) return ctx.ir_gen->wrap(out);
            llvm::Value* ty = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            ctx.builder.CreateCall(
                ctx.getFunction("aiml_cascade_type_range", i32, {i8p, ptrTy}), {ty, out});
            return ctx.ir_gen->wrap(out);
        };

        // cascade.describe(cascade: string, opcodes?: list<i32>) → list<string>
        //   Data type combinations of the opcodes (opcodes empty/missing → ALL).
        //   Element 0 = opcode enum name (e.g. "EffectCode"); after that one
        //   line per opcode "[id]node [range]Type …".
        cas.builtinFuncs["describe"] = [](CompilerContext& ctx,
                                          const std::vector<IRValue*>& args) -> IRValue* {
            auto* i32   = ctx.builder.getInt32Ty();
            auto* i8p   = ctx.builder.getInt8Ty()->getPointerTo();
            auto* ptrTy = llvm::PointerType::get(ctx.context, 0);
            llvm::StructType* listTy = ListImpl::getListStructType(ctx);
            // spec/61 §5 (memV3): return a HEAP handle, not a stack alloca (see field_indices).
            llvm::Value* out;
            if (ctx.memV3) {
                out = ctx.builder.CreateCall(ctx.getFunction("aiml_list_handle_new", i8p, {}), {}, "casc_desc_h");
                ListImpl::initList(ctx, out, i8p, 8);
            } else {
                out = ctx.createEntryAlloca(listTy, "casc_desc_out");
                ListImpl::initList(ctx, out, i8p, 8);
            }
            if (args.empty()) return ctx.ir_gen->wrap(out);
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            // Optionale Opcode-list<i32> → (data, len); fehlt → (null, 0) = alle.
            llvm::Value* opD = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p));
            llvm::Value* opN = ctx.builder.getInt32(0);
            if (args.size() >= 2) {
                llvm::Value* ol = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
                llvm::Value* olP = ol;
                if (!ol->getType()->isPointerTy()) {
                    llvm::IRBuilder<> tmp(&ctx.builder.GetInsertBlock()->getParent()->getEntryBlock(),
                                          ctx.builder.GetInsertBlock()->getParent()->getEntryBlock().begin());
                    llvm::AllocaInst* al = tmp.CreateAlloca(ol->getType(), nullptr, "opc_spill");
                    ctx.builder.CreateStore(ol, al);
                    olP = al;
                }
                llvm::Value* szGep = ctx.builder.CreateStructGEP(listTy, olP, 0, "opc_size_gep");
                opN = ctx.builder.CreateLoad(i32, szGep, "opc_len");
                llvm::Value* dGep = ctx.builder.CreateStructGEP(listTy, olP, 2, "opc_data_gep");
                opD = ctx.builder.CreateLoad(i8p, dGep, "opc_data");
            }
            ctx.builder.CreateCall(
                ctx.getFunction("aiml_cascade_describe", i32, {i8p, i8p, i32, ptrTy}),
                {name, opD, opN, out});
            return ctx.ir_gen->wrap(out);
        };
    }

    // -----------------------------------------------------------------------
    // enums namespace — compile-time enum reflection (spec/14_enums.md)
    //   enums.name(type: string-literal, value: i32)      → string  (value → member name)
    //   enums.value(type: string-literal, member: string) → i32     (member name → value)
    // Resolved from CompilerContext at compile time; enums.name lowers to a
    // select-chain (no runtime table).  enums.name(T,…) marks the WHOLE enum T
    // referenced for the unused-enumerator analysis (spec/49 §18).
    // -----------------------------------------------------------------------
    {
        auto& en = ctx.namespaces["enums"];

        // Extract a compile-time string literal from an evaluated arg (a global
        // string constant), or "" if the value is not a literal.
        auto litStr = [](llvm::Value* v) -> std::string {
            if (!v) return {};
            v = v->stripPointerCasts();
            if (auto* gv = llvm::dyn_cast<llvm::GlobalVariable>(v)) {
                if (gv->hasInitializer()) {
                    if (auto* cda = llvm::dyn_cast<llvm::ConstantDataArray>(gv->getInitializer())) {
                        std::string s = cda->getAsString().str();
                        if (!s.empty() && s.back() == '\0') s.pop_back();
                        return s;
                    }
                }
            }
            return {};
        };

        // Resolve an enum name to its (registry key, member-map); tries the bare
        // name and the caller-qualified name.  nullptr if unknown.
        auto findEnum = [](CompilerContext& ctx, const std::string& typeName,
                           std::string& outKey) -> std::map<std::string,int>* {
            auto it = ctx.enums.find(typeName);
            if (it != ctx.enums.end()) { outKey = it->first; return &it->second; }
            it = ctx.enums.find(ctx.qualify(typeName));
            if (it != ctx.enums.end()) { outKey = it->first; return &it->second; }
            return nullptr;
        };

        en.builtinFuncs["name"] = [litStr, findEnum](CompilerContext& ctx,
                                    const std::vector<IRValue*>& args) -> IRValue* {
            if (args.size() < 2) {
                LOG_ERROR(ctx.fmtLoc() + "enums.name: expected (type, value)");
                return ctx.ir_gen->wrap(ctx.builder.CreateGlobalStringPtr(""));
            }
            std::string typeName = litStr(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])));
            if (typeName.empty()) {
                LOG_ERROR(ctx.fmtLoc() + "enums.name: 'type' argument must be a string literal");
                return ctx.ir_gen->wrap(ctx.builder.CreateGlobalStringPtr(""));
            }
            std::string key;
            auto* em = findEnum(ctx, typeName, key);
            if (!em) {
                LOG_ERROR(ctx.fmtLoc() + "enums.name: unknown enum type '" + typeName + "'");
                return ctx.ir_gen->wrap(ctx.builder.CreateGlobalStringPtr(""));
            }
            // Reflective whole-enum use (spec/49 §18): mark every member referenced.
            for (const auto& kv : *em) ctx.enumUsedMembers[key].insert(kv.first);
            llvm::Value* val = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            if (val->getType() != ctx.builder.getInt32Ty())
                val = ctx.builder.CreateZExtOrTrunc(val, ctx.builder.getInt32Ty());
            // Select-chain: value → member name, default "".
            llvm::Value* result = ctx.builder.CreateGlobalStringPtr("");
            for (const auto& kv : *em) {
                llvm::Value* cond = ctx.builder.CreateICmpEQ(val, ctx.builder.getInt32(kv.second));
                llvm::Value* nm   = ctx.builder.CreateGlobalStringPtr(kv.first);
                result = ctx.builder.CreateSelect(cond, nm, result);
            }
            return ctx.ir_gen->wrap(result);
        };

        en.builtinFuncs["value"] = [litStr, findEnum](CompilerContext& ctx,
                                     const std::vector<IRValue*>& args) -> IRValue* {
            if (args.size() < 2) {
                LOG_ERROR(ctx.fmtLoc() + "enums.value: expected (type, member)");
                return ctx.ir_gen->getInt32(0);
            }
            std::string typeName = litStr(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])));
            std::string member   = litStr(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])));
            if (typeName.empty() || member.empty()) {
                LOG_ERROR(ctx.fmtLoc() + "enums.value: 'type' and 'member' must be string literals");
                return ctx.ir_gen->getInt32(0);
            }
            std::string key;
            auto* em = findEnum(ctx, typeName, key);
            if (!em) {
                LOG_ERROR(ctx.fmtLoc() + "enums.value: unknown enum type '" + typeName + "'");
                return ctx.ir_gen->getInt32(0);
            }
            auto mi = em->find(member);
            if (mi == em->end()) {
                LOG_ERROR(ctx.fmtLoc() + "enums.value: enum '" + typeName + "' has no member '" + member + "'");
                return ctx.ir_gen->getInt32(0);
            }
            ctx.enumUsedMembers[key].insert(member);
            return ctx.ir_gen->getInt32(mi->second);
        };
    }

    // -----------------------------------------------------------------------
    // sys namespace
    // -----------------------------------------------------------------------
    auto& sys = ctx.namespaces["sys"];

    // null_ptr() → ptr  —  returns a typed null pointer (i8*); useful for FFI
    // calls that require a NULL argument (e.g. glfwCreateWindow monitor/share).
    sys.builtinFuncs["null_ptr"] = [](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
        auto* i8p = static_cast<llvm::PointerType*>(llvm::PointerType::get(ctx.context, 0));
        return ctx.ir_gen->wrap(llvm::ConstantPointerNull::get(i8p));
    };

    sys.builtinFuncs["exit"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        llvm::Function* f = ctx.getFunction("exit", ctx.builder.getVoidTy(), {ctx.builder.getInt32Ty()});
        llvm::Value* code = args.empty() ? ctx.builder.getInt32(0)
                                         : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        ctx.builder.CreateCall(f, {code});
        return ctx.ir_gen->getInt32(0);
    };

    // sys.mem_limit(bytes_i64) — set process virtual memory limit (RLIMIT_AS).
    // Process is terminated when exceeded instead of freezing the machine.
    sys.builtinFuncs["mem_limit"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        llvm::Function* f = ctx.getFunction("aiml_sys_mem_limit",
            ctx.builder.getVoidTy(), {ctx.builder.getInt64Ty()});
        llvm::Value* bytes = args.empty() ? ctx.builder.getInt64(0)
                                          : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        // Widen i32 to i64 if needed
        if (bytes->getType()->isIntegerTy(32))
            bytes = ctx.builder.CreateSExt(bytes, ctx.builder.getInt64Ty());
        ctx.builder.CreateCall(f, {bytes});
        return ctx.ir_gen->getInt32(0);
    };

    // sys.mem_usage() → i64 — current RSS (resident set size) in bytes.
    sys.builtinFuncs["mem_usage"] = [](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
        llvm::Function* f = ctx.getFunction("aiml_sys_mem_usage",
            ctx.builder.getInt64Ty(), {});
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(f));
    };

    // sys.mem_diff() → i64 — AIML_MEM_DIFF: print the live-allocation diff (top sites by growth
    // since the last call, symbolized by function) to stderr and return total live bytes. No-op
    // returning 0 when the AIML_MEM_DIFF env var is unset. Call it from the periodic [mem] tick to
    // watch what accumulates. See spec/48.
    sys.builtinFuncs["mem_diff"] = [](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
        llvm::Function* f = ctx.getFunction("aiml_sys_mem_diff",
            ctx.builder.getInt64Ty(), {});
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(f));
    };
    // spec/23 §4.1 — host parallelism: three questions, three functions.  Call form
    // only; a property form would be exactly the sys.env bug (documented
    // property, emitted null pointer) once more.
    sys.builtinFuncs["core_count"] = [](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
        llvm::Function* f = ctx.getFunction("aiml_sys_core_count",
            ctx.builder.getInt32Ty(), {});
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(f));
    };

    sys.builtinFuncs["logical_thread_count"] = [](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
        llvm::Function* f = ctx.getFunction("aiml_sys_logical_thread_count",
            ctx.builder.getInt32Ty(), {});
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(f));
    };

    sys.builtinFuncs["quota_thread_count"] = [](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
        llvm::Function* f = ctx.getFunction("aiml_sys_quota_thread_count",
            ctx.builder.getInt32Ty(), {});
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(f));
    };


    sys.builtinFuncs["env"] = [](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
        // Delegate to the runtime, which populates a map<string:string> from
        // `environ`.  The returned AimlHttpMap* shares map<K:V>'s memory layout,
        // so AIML map methods (get/contains/…) operate on it transparently.
        llvm::Type* strTy = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::StructType* mapTy = llvm::StructType::get(ctx.context, {
            ctx.builder.getInt32Ty(), ctx.builder.getInt32Ty(),
            ctx.builder.getInt8Ty()->getPointerTo(),
            strTy->getPointerTo(), strTy->getPointerTo()
        });
        llvm::Function* f = ctx.getFunction("aiml_sys_env",
            ctx.builder.getInt8Ty()->getPointerTo(), {});
        llvm::Value* raw = ctx.builder.CreateCall(f);
        llvm::Value* mapPtr = ctx.builder.CreateBitCast(raw, mapTy->getPointerTo(), "sys_env_map");
        return ctx.ir_gen->wrap(mapPtr);
    };

    sys.builtinFuncs["size"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        if (args.empty()) return ctx.ir_gen->getInt32(0);
        llvm::Type* ty = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(args[0]->getType()));
        llvm::DataLayout dl(&ctx.module());
        return ctx.ir_gen->wrap(ctx.builder.getInt64(dl.getTypeAllocSize(ty)));
    };

    // spec/23 §5.1: sys.exe(argv [, option bag]) — the bag is parsed into ctx.exeOpts by visitCall.
    sys.builtinFuncs["exe"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32 = ctx.builder.getInt32Ty();
        llvm::Value* nul = llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p));
        if (args.empty()) return ctx.ir_gen->wrap(nul);
        llvm::Value* argvList = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        if (argvList->getType()->isStructTy()) {
            llvm::AllocaInst* tmp = ctx.createEntryAlloca(argvList->getType());
            ctx.builder.CreateStore(argvList, tmp);
            argvList = ctx.builder.CreateBitCast(tmp, i8p);
        } else if (argvList->getType()->isPointerTy()) {
            argvList = ctx.builder.CreateBitCast(argvList, i8p);
        } else {
            return ctx.ir_gen->wrap(nul);
        }
        const auto& o = ctx.exeOpts;
        auto ptrOr = [&](llvm::Value* v) { return v ? ctx.builder.CreateBitCast(v, i8p) : nul; };
        llvm::Function* f = ctx.getFunction("aiml_process_spawn", i8p,
            {i8p, i8p, i8p, i32, i32, i32, i8p, i8p, i8p, i32, i8p});
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
            argvList, ptrOr(o.cwd), ptrOr(o.env), ctx.builder.getInt32(o.shell ? 1 : 0),
            o.timeout ? o.timeout : ctx.builder.getInt32(-1), ctx.builder.getInt32(o.capture ? 1 : 0),
            ptrOr(o.stdinPath), ptrOr(o.stdoutPath), ptrOr(o.stderrPath), ctx.builder.getInt32(o.detach ? 1 : 0),
            ptrOr(o.statusFile)}));
    };

    // spec/23 §5.5: sys.wait_any(procs: list<process>, timeout_ms) → index of an ended process, -1
    sys.builtinFuncs["wait_any"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32 = ctx.builder.getInt32Ty();
        if (args.empty()) return ctx.ir_gen->getInt32(-1);
        llvm::Value* procs = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        if (procs->getType()->isStructTy()) {                 // a stack list value: pass its address
            llvm::AllocaInst* tmp = ctx.createEntryAlloca(procs->getType());
            ctx.builder.CreateStore(procs, tmp);
            procs = tmp;
        }
        llvm::Value* wait = args.size() > 1 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])) : ctx.builder.getInt32(-1);
        if (wait->getType()->isIntegerTy() && wait->getType() != i32) wait = ctx.builder.CreateIntCast(wait, i32, true);
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(ctx.getFunction("aiml_sys_wait_any", i32, {i8p, i32}),
                                                       {ctx.builder.CreateBitCast(procs, i8p), wait}));
    };
    // spec/23 §5.7: sys.catch_signals("INT,TERM") → 0 / -1;  sys.caught_signal() → last caught, cleared
    sys.builtinFuncs["catch_signals"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Value* names = args.empty() ? static_cast<llvm::Value*>(ctx.builder.CreateGlobalStringPtr(""))
                                          : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(ctx.getFunction("aiml_sys_catch_signals", ctx.builder.getInt32Ty(), {i8p}), {names}));
    };
    sys.builtinFuncs["caught_signal"] = [](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(ctx.getFunction("aiml_sys_caught_signal", ctx.builder.getInt32Ty(), {}), {}));
    };

    // spec/23 §5.4: sys.process_attach(pid, status_file) → process (re-acquire a detached job)
    sys.builtinFuncs["process_attach"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i64 = ctx.builder.getInt64Ty();
        if (args.size() < 2) return ctx.ir_gen->wrap(llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p)));
        llvm::Value* pid = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        if (pid->getType()->isIntegerTy() && !pid->getType()->isIntegerTy(64)) pid = ctx.builder.CreateSExt(pid, i64);
        llvm::Value* st = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
        llvm::Function* f = ctx.getFunction("aiml_process_attach", i8p, {i64, i8p});
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {pid, st}));
    };

    // spec/65 F65-37: sys.export_symbol(name, fn, level) → i32
    // The host offers a symbol of its own.  ONE call for both halves of the
    // handover: address (AddSymbol, so that the JIT resolves it) and name+level (the
    // export table the access check reads from).  Two calls would be two
    // lists that drift (F65-23).
    //
    // No guard of its own needed: `sys` is closed as a namespace (F65-35), a
    // restricted session is already refused at the dispatch point with `clearance-builtin`.
    // A script that could export itself would set its own level.
    sys.builtinFuncs["export_symbol"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32 = ctx.builder.getInt32Ty();
        if (args.size() < 3) {
            LOG_ERROR(ctx.fmtLoc() + "sys.export_symbol expects (name, fn, level) — spec/65 F65-37");
            return ctx.ir_gen->getInt32(-1);
        }
        llvm::Value* nm = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        llvm::Value* fp = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
        llvm::Value* lv = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]));
        if (nm->getType()->isPointerTy()) nm = ctx.builder.CreateBitCast(nm, i8p);
        if (fp->getType()->isPointerTy()) fp = ctx.builder.CreateBitCast(fp, i8p);
        if (lv->getType()->isIntegerTy() && !lv->getType()->isIntegerTy(32))
            lv = ctx.builder.CreateIntCast(lv, i32, true);
        llvm::Function* f = ctx.getFunction("aiml_clearance_export", i32, {i8p, i8p, i32});
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {nm, fp, lv}));
    };

    // spec/65 F65-32: the session's policy is READABLE, never writable.  A setter
    // would be self-certification (F65-6) — the script would grant itself a right.  Reading
    // is harmless (levels are not secret, F65-16) and necessary so that a script can give a
    // usable message instead of failing on a compiler diagnostic.
    // The namespace stays CLOSED in `clearance_builtins.h`; only these three are open.
    {
        auto& clr = ctx.namespaces["clearance"];
        auto reader = [](const char* rt) {
            return [rt](CompilerContext& c, const std::vector<IRValue*>&) -> IRValue* {
                llvm::Type* i32 = c.builder.getInt32Ty();
                return c.ir_gen->wrap(c.builder.CreateCall(c.getFunction(rt, i32, {}), {}));
            };
        };
        clr.builtinFuncs["level"]         = reader("aiml_clearance_level");
        clr.builtinFuncs["create_level"]  = reader("aiml_clearance_create_level");
        clr.builtinFuncs["replace_level"] = reader("aiml_clearance_replace_level");
    }

    // sys.get_arg uses a special outlined helper — registered separately because
    // it needs the __argc/__argv globals (eagerly evaluated index passed as arg).
    // spec/23: sys.exe_path() → runtime-owned string
    sys.builtinFuncs["exe_path"] = [](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
        auto* p = llvm::PointerType::get(ctx.context, 0);
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(ctx.getFunction("aiml_sys_exe_path", p, {}), {}));
    };
    // spec/29 §5.2d: sys.json_error() → what the last sys.deserialize on this thread
    // found wrong (syntax with line/column, wrong types with the field path), "" = clean.
    sys.builtinFuncs["json_error"] = [](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
        auto* p = llvm::PointerType::get(ctx.context, 0);
        return ownedStringResult(ctx, ctx.builder.CreateCall(ctx.getFunction("aiml_sys_json_error", p, {}), {}));
    };
    // spec/23 F23-78: sys.format_fixed(value, decimals) → string with exactly that many
    // decimals ("0.333"), the one way to line a column of reals up.
    sys.builtinFuncs["format_fixed"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        auto* p = llvm::PointerType::get(ctx.context, 0);
        llvm::Type* d = ctx.builder.getDoubleTy();
        llvm::Type* i32Ty = ctx.builder.getInt32Ty();
        if (args.empty()) {
            LOG_ERROR(ctx.fmtLoc() + "sys.format_fixed expects (value, decimals) (spec/23 F23-78)");
            return ctx.ir_gen->wrap(ctx.builder.CreateGlobalStringPtr(""));
        }
        llvm::Value* raw = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        llvm::Value* val = raw->getType()->isDoubleTy() ? raw
                         : raw->getType()->isFloatingPointTy() ? ctx.builder.CreateFPExt(raw, d)
                         : ctx.builder.CreateSIToFP(raw, d);
        llvm::Value* dec = ctx.builder.getInt32(6);
        if (args.size() > 1) {
            llvm::Value* dr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            dec = dr->getType()->isIntegerTy() ? ctx.builder.CreateIntCast(dr, i32Ty, true)
                                               : ctx.builder.CreateFPToSI(dr, i32Ty);
        }
        return ownedStringResult(ctx, ctx.builder.CreateCall(
            ctx.getFunction("aiml_sys_format_fixed", p, {d, i32Ty}), {val, dec}));
    };
    // spec/23 §5.6: sys.require_tools(csv) → "" or EVERYTHING that is missing.  One place
    // for this question (F23-43): `requires:` (spec/62 §18.2) and the cosmos
    // agent call the same code instead of maintaining two checks.
    sys.builtinFuncs["require_tools"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        auto* p = llvm::PointerType::get(ctx.context, 0);
        llvm::Value* spec = args.empty()
            ? static_cast<llvm::Value*>(ctx.builder.CreateGlobalStringPtr("", "req_empty"))
            : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        llvm::Value* out = ctx.builder.CreateCall(
            ctx.getFunction("aiml_sys_require_tools", p, {p}), {spec}, "req_tools");
        if (ctx.lifetimeDropsEnabled && !ctx.stmtTempStack.empty())
            AIML::AST::aimlTrackStringTemp(ctx, out);
        return ctx.ir_gen->wrap(out);
    };
    // spec/23 §3 F23-60: sys.arg_count() → argc (argv[0] included); 0 when unknown.
    sys.builtinFuncs["arg_count"] = [](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
        llvm::GlobalVariable* argcVar = ctx.module().getGlobalVariable("__argc", true);
        if (!argcVar) return ctx.ir_gen->getInt32(0);
        return ctx.ir_gen->wrap(ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), argcVar, "argc"));
    };
    sys.builtinFuncs["get_arg"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        if (args.empty()) return ctx.ir_gen->createGlobalStringPtr("");
        llvm::Value* indexVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        llvm::GlobalVariable* argvVar = ctx.module().getGlobalVariable("__argv", true);
        llvm::GlobalVariable* argcVar = ctx.module().getGlobalVariable("__argc", true);
        if (!argvVar || !argcVar) return ctx.ir_gen->createGlobalStringPtr("");

        llvm::Function* impl = ctx.module().getFunction("__aiml_sys_get_arg");
        if (!impl) {
            llvm::PointerType* i8p    = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::PointerType* i8pp   = i8p->getPointerTo();
            llvm::Type* i32           = ctx.builder.getInt32Ty();
            llvm::IRBuilder<>::InsertPoint savedIP = ctx.builder.saveIP();
            llvm::FunctionType* ft = llvm::FunctionType::get(i8p, {i32}, false);
            impl = llvm::Function::Create(ft, llvm::Function::InternalLinkage, "__aiml_sys_get_arg", ctx.module());
            impl->addFnAttr(llvm::Attribute::NoInline);
            llvm::BasicBlock* entryBB = llvm::BasicBlock::Create(ctx.context, "entry",     impl);
            llvm::BasicBlock* safeBB  = llvm::BasicBlock::Create(ctx.context, "arg_safe",  impl);
            llvm::BasicBlock* errorBB = llvm::BasicBlock::Create(ctx.context, "arg_error", impl);
            llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(ctx.context, "arg_merge", impl);
            ctx.builder.SetInsertPoint(entryBB);
            llvm::Value* idxArg  = impl->getArg(0);
            llvm::Value* argvPtr = ctx.builder.CreateLoad(i8pp, argvVar);
            llvm::Value* argcVal = ctx.builder.CreateLoad(i32, argcVar);
            llvm::Value* argvIsNull = ctx.builder.CreateICmpEQ(argvPtr, llvm::ConstantPointerNull::get(i8pp));
            llvm::Value* idxOk   = ctx.builder.CreateICmpSLT(idxArg, argcVal);
            llvm::Value* isSafe  = ctx.builder.CreateAnd(ctx.builder.CreateNot(argvIsNull), idxOk);
            ctx.builder.CreateCondBr(isSafe, safeBB, errorBB);
            ctx.builder.SetInsertPoint(safeBB);
            llvm::Value* argStrPtr = ctx.builder.CreateInBoundsGEP(i8p, argvPtr, idxArg);
            llvm::Value* argStr    = ctx.builder.CreateLoad(i8p, argStrPtr);
            ctx.builder.CreateBr(mergeBB);
            ctx.builder.SetInsertPoint(errorBB);
            llvm::Value* emptyStr = ctx.builder.CreateGlobalStringPtr("");
            ctx.builder.CreateBr(mergeBB);
            ctx.builder.SetInsertPoint(mergeBB);
            llvm::PHINode* result = ctx.builder.CreatePHI(i8p, 2);
            result->addIncoming(argStr, safeBB);
            result->addIncoming(emptyStr, errorBB);
            ctx.builder.CreateRet(result);
            ctx.builder.restoreIP(savedIP);
        }
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(impl, {indexVal}));
    };

    // sys.load_lib(path_str) → ptr   (dlopen wrapper)
    sys.builtinFuncs["load_lib"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        auto* i8p  = llvm::PointerType::get(ctx.context, 0);
        auto* i32  = ctx.builder.getInt32Ty();
        // Declare dlopen if needed: void* dlopen(const char*, int)
        llvm::Function* dlopen_fn = ctx.module().getFunction("dlopen");
        if (!dlopen_fn) {
            auto* ft = llvm::FunctionType::get(i8p, {i8p, i32}, false);
            dlopen_fn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "dlopen", ctx.module());
        }
        llvm::Value* pathArg = args.empty()
            ? llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p))
            : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        // RTLD_LAZY = 1
        llvm::Value* handle = ctx.builder.CreateCall(dlopen_fn, {pathArg, ctx.builder.getInt32(1)});
        return ctx.ir_gen->wrap(handle);
    };

    // sys.load_sym(handle: ptr, name: string) → ptr   (dlsym wrapper)
    sys.builtinFuncs["load_sym"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        auto* i8p = llvm::PointerType::get(ctx.context, 0);
        // Declare dlsym if needed: void* dlsym(void*, const char*)
        llvm::Function* dlsym_fn = ctx.module().getFunction("dlsym");
        if (!dlsym_fn) {
            auto* ft = llvm::FunctionType::get(i8p, {i8p, i8p}, false);
            dlsym_fn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "dlsym", ctx.module());
        }
        llvm::Value* handle = args.size() > 0
            ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))
            : llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p));
        llvm::Value* name = args.size() > 1
            ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
            : ctx.builder.CreateGlobalStringPtr("");
        llvm::Value* sym = ctx.builder.CreateCall(dlsym_fn, {handle, name});
        return ctx.ir_gen->wrap(sym);
    };

    // -----------------------------------------------------------------------
    // io namespace
    // -----------------------------------------------------------------------
    auto& io = ctx.namespaces["io"];

    io.builtinFuncs["open"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        if (args.size() < 2) return ctx.ir_gen->wrap(llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p)));
        llvm::Function* f = ctx.getFunction("aiml_io_open", i8p, {i8p, i8p});   // fopen + close-on-exec
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
            static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
            static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))}));
    };

    io.builtinFuncs["close"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        if (args.empty()) return ctx.ir_gen->getInt32(0);
        llvm::Function* f = ctx.getFunction("fclose",
            ctx.builder.getInt32Ty(), {ctx.builder.getInt8Ty()->getPointerTo()});
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(f,
            {static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))}));
    };

    io.builtinFuncs["write"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        if (args.size() < 2) return ctx.ir_gen->getInt32(0);
        llvm::Type* i8p  = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Value* handle = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        llvm::Value* text   = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
        llvm::Function* parentFunc  = ctx.builder.GetInsertBlock()->getParent();
        llvm::BasicBlock* validBB   = llvm::BasicBlock::Create(ctx.context, "valid_write_handle",   parentFunc);
        llvm::BasicBlock* invalidBB = llvm::BasicBlock::Create(ctx.context, "invalid_write_handle", parentFunc);
        llvm::BasicBlock* mergeBB   = llvm::BasicBlock::Create(ctx.context, "merge_write",          parentFunc);
        llvm::Value* isNull = ctx.builder.CreateICmpEQ(handle, llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p)));
        ctx.builder.CreateCondBr(isNull, invalidBB, validBB);
        ctx.builder.SetInsertPoint(invalidBB);
        ctx.builder.CreateBr(mergeBB);
        ctx.builder.SetInsertPoint(validBB);
        llvm::Function* strlenF = ctx.getFunction("strlen", ctx.getSizeType(), {i8p});
        llvm::Value* len = ctx.builder.CreateCall(strlenF, {text});
        llvm::Function* fwF = ctx.getFunction("fwrite", ctx.getSizeType(),
            {i8p, ctx.getSizeType(), ctx.getSizeType(), i8p});
        llvm::Value* written = ctx.builder.CreateCall(fwF, {text, ctx.getSizeConst(1), len, handle});
        ctx.builder.CreateBr(mergeBB);
        ctx.builder.SetInsertPoint(mergeBB);
        llvm::PHINode* phi = ctx.builder.CreatePHI(ctx.getSizeType(), 2, "write_res");
        phi->addIncoming(ctx.getSizeConst(0), invalidBB);
        phi->addIncoming(written, validBB);
        return ctx.ir_gen->wrap(ctx.builder.CreateZExtOrTrunc(phi, ctx.builder.getInt32Ty()));
    };

    io.builtinFuncs["read"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        if (args.size() < 2) return ctx.ir_gen->wrap(ctx.builder.CreateGlobalStringPtr(""));
        llvm::Value* handle = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        llvm::Function* parentFunc  = ctx.builder.GetInsertBlock()->getParent();
        llvm::BasicBlock* validBB   = llvm::BasicBlock::Create(ctx.context, "valid_handle",   parentFunc);
        llvm::BasicBlock* invalidBB = llvm::BasicBlock::Create(ctx.context, "invalid_handle", parentFunc);
        llvm::BasicBlock* mergeBB   = llvm::BasicBlock::Create(ctx.context, "merge_read",     parentFunc);
        llvm::Value* isNull = ctx.builder.CreateICmpEQ(handle, llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p)));
        ctx.builder.CreateCondBr(isNull, invalidBB, validBB);
        ctx.builder.SetInsertPoint(invalidBB);
        llvm::Value* emptyStr = ctx.builder.CreateGlobalStringPtr("");
        ctx.builder.CreateBr(mergeBB);
        ctx.builder.SetInsertPoint(validBB);
        llvm::Value* size = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
        size = ctx.toSizeType(size);
        llvm::BasicBlock* doReadBB = llvm::BasicBlock::Create(ctx.context, "do_read", parentFunc);
        ctx.builder.CreateCondBr(ctx.builder.CreateICmpSGT(size, ctx.getSizeConst(0)), doReadBB, invalidBB);
        ctx.builder.SetInsertPoint(doReadBB);
        llvm::Function* mallocF = ctx.getFunction("malloc", i8p, {ctx.getSizeType()});
        llvm::Value* buf = ctx.builder.CreateCall(mallocF, {ctx.builder.CreateAdd(size, ctx.getSizeConst(1))});
        llvm::Function* freadF = ctx.getFunction("fread", ctx.getSizeType(),
            {i8p, ctx.getSizeType(), ctx.getSizeType(), i8p});
        llvm::Value* nread = ctx.builder.CreateCall(freadF, {buf, ctx.getSizeConst(1), size, handle});
        ctx.builder.CreateStore(ctx.builder.getInt8(0),
            ctx.builder.CreateInBoundsGEP(ctx.builder.getInt8Ty(), buf, nread));
        ctx.builder.CreateBr(mergeBB);
        ctx.builder.SetInsertPoint(mergeBB);
        llvm::PHINode* phi = ctx.builder.CreatePHI(i8p, 2, "read_res");
        phi->addIncoming(emptyStr, invalidBB);
        phi->addIncoming(buf, doReadBB);
        return ctx.ir_gen->wrap(phi);
    };

    io.builtinFuncs["read_all"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        if (args.empty()) return ctx.ir_gen->wrap(ctx.builder.CreateGlobalStringPtr(""));
        llvm::Value* handle = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        llvm::Function* parentFunc  = ctx.builder.GetInsertBlock()->getParent();
        llvm::BasicBlock* validBB   = llvm::BasicBlock::Create(ctx.context, "valid_handle",   parentFunc);
        llvm::BasicBlock* invalidBB = llvm::BasicBlock::Create(ctx.context, "invalid_handle", parentFunc);
        llvm::BasicBlock* mergeBB   = llvm::BasicBlock::Create(ctx.context, "merge_read_all", parentFunc);
        llvm::Value* isNull = ctx.builder.CreateICmpEQ(handle, llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p)));
        ctx.builder.CreateCondBr(isNull, invalidBB, validBB);
        ctx.builder.SetInsertPoint(invalidBB);
        llvm::Value* emptyStr = ctx.builder.CreateGlobalStringPtr("");
        ctx.builder.CreateBr(mergeBB);
        ctx.builder.SetInsertPoint(validBB);
        llvm::Function* fseekF = ctx.getFunction("fseek", ctx.builder.getInt32Ty(),
            {i8p, ctx.getSizeType(), ctx.builder.getInt32Ty()});
        llvm::Function* ftellF = ctx.getFunction("ftell", ctx.getSizeType(), {i8p});
        llvm::Function* rewindF = ctx.getFunction("rewind", ctx.builder.getVoidTy(), {i8p});
        ctx.builder.CreateCall(fseekF, {handle, ctx.getSizeConst(0), ctx.builder.getInt32(2)});
        llvm::Value* size = ctx.builder.CreateCall(ftellF, {handle});
        ctx.builder.CreateCall(rewindF, {handle});
        llvm::BasicBlock* doReadBB = llvm::BasicBlock::Create(ctx.context, "do_read_all", parentFunc);
        ctx.builder.CreateCondBr(ctx.builder.CreateICmpSGT(size, ctx.getSizeConst(0)), doReadBB, invalidBB);
        ctx.builder.SetInsertPoint(doReadBB);
        llvm::Function* mallocF = ctx.getFunction("malloc", i8p, {ctx.getSizeType()});
        llvm::Value* buf = ctx.builder.CreateCall(mallocF, {ctx.builder.CreateAdd(size, ctx.getSizeConst(1))});
        llvm::Function* freadF = ctx.getFunction("fread", ctx.getSizeType(),
            {i8p, ctx.getSizeType(), ctx.getSizeType(), i8p});
        llvm::Value* nread = ctx.builder.CreateCall(freadF, {buf, ctx.getSizeConst(1), size, handle});
        ctx.builder.CreateStore(ctx.builder.getInt8(0),
            ctx.builder.CreateInBoundsGEP(ctx.builder.getInt8Ty(), buf, nread));
        // spec/61 §4: return an aiml-MANAGED string (with an AimlStrHeader), not a bare malloc buffer —
        // the caller binds the result to a managed string local whose assign/drop calls
        // aiml_str_incref/decref, which read the rc header at (s - 8). A header-less malloc buffer makes
        // those touch 8 bytes out of bounds (valgrind Invalid read; a chance magic match would corrupt).
        // Copy into an aiml string, then free the raw buffer.
        llvm::Function* strDupF = ctx.getFunction("aiml_str_dup", i8p, {i8p});
        llvm::Function* freeF   = ctx.getFunction("free", ctx.builder.getVoidTy(), {i8p});
        llvm::Value* managed = ctx.builder.CreateCall(strDupF, {buf}, "read_all_managed");
        ctx.builder.CreateCall(freeF, {buf});
        ctx.builder.CreateBr(mergeBB);
        // Managed empty string for the invalid/empty path (same header contract as `managed`).
        ctx.builder.SetInsertPoint(invalidBB->getTerminator());  // (invalidBB already branches to merge)
        llvm::Value* emptyManaged = ctx.builder.CreateCall(strDupF, {emptyStr}, "read_all_empty_managed");
        ctx.builder.SetInsertPoint(mergeBB);
        llvm::PHINode* phi = ctx.builder.CreatePHI(i8p, 2, "read_all_res");
        phi->addIncoming(emptyManaged, invalidBB);
        phi->addIncoming(managed, doReadBB);
        // spec/61 §7: the result is a FRESH owned aiml string → register as an owned stmt-temp so a
        // binding local MOVES it (freed by its scope-drop) instead of the decl-init treating it as a
        // borrow and increfing it (→ leak). Same as the yaml/str string builtins.
        if (ctx.lifetimeDropsEnabled && !ctx.stmtTempStack.empty())
            AIML::AST::aimlTrackStringTemp(ctx, phi);
        return ctx.ir_gen->wrap(phi);
    };

    io.builtinFuncs["open_binary"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        if (args.empty()) return ctx.ir_gen->wrap(llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p)));
        llvm::Function* fn = ctx.getFunction("aiml_io_open_binary", i8p, {i8p});
        llvm::Value* path = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {path}));
    };

    io.builtinFuncs["close"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        if (args.empty()) return ctx.ir_gen->getInt32(0);
        llvm::Function* fn = ctx.getFunction("aiml_io_close", ctx.builder.getVoidTy(), {i8p});
        llvm::Value* f = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        ctx.builder.CreateCall(fn, {f});
        return ctx.ir_gen->getInt32(0);
    };

    io.builtinFuncs["tell"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        if (args.empty()) return ctx.ir_gen->wrap(ctx.builder.getInt64(0));
        llvm::Function* fn = ctx.getFunction("aiml_io_tell", ctx.builder.getInt64Ty(), {i8p});
        llvm::Value* f = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {f}));
    };

    io.builtinFuncs["seek"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        if (args.size() < 2) return ctx.ir_gen->getInt32(0);
        llvm::Function* fn = ctx.getFunction("aiml_io_seek", ctx.builder.getVoidTy(), {i8p, ctx.builder.getInt64Ty()});
        llvm::Value* f   = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        llvm::Value* pos = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
        if (pos->getType()->isIntegerTy(32)) pos = ctx.builder.CreateSExt(pos, ctx.builder.getInt64Ty());
        ctx.builder.CreateCall(fn, {f, pos});
        return ctx.ir_gen->getInt32(0);
    };

    io.builtinFuncs["skip"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        if (args.size() < 2) return ctx.ir_gen->getInt32(0);
        llvm::Function* fn = ctx.getFunction("aiml_io_skip", ctx.builder.getVoidTy(), {i8p, ctx.builder.getInt64Ty()});
        llvm::Value* f     = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        llvm::Value* delta = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
        if (delta->getType()->isIntegerTy(32)) delta = ctx.builder.CreateSExt(delta, ctx.builder.getInt64Ty());
        ctx.builder.CreateCall(fn, {f, delta});
        return ctx.ir_gen->getInt32(0);
    };

    io.builtinFuncs["eof"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        if (args.empty()) return ctx.ir_gen->getInt32(1);
        llvm::Function* fn = ctx.getFunction("aiml_io_eof", ctx.builder.getInt32Ty(), {i8p});
        llvm::Value* f = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {f}));
    };

    io.builtinFuncs["read_u8"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        if (args.empty()) return ctx.ir_gen->getInt32(0);
        llvm::Function* fn = ctx.getFunction("aiml_io_read_u8", ctx.builder.getInt32Ty(), {i8p});
        llvm::Value* f = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {f}));
    };

    io.builtinFuncs["read_bool"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        if (args.empty()) return ctx.ir_gen->getInt32(0);
        llvm::Function* fn = ctx.getFunction("aiml_io_read_bool", ctx.builder.getInt32Ty(), {i8p});
        llvm::Value* f = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {f}));
    };

    io.builtinFuncs["read_i32_le"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        if (args.empty()) return ctx.ir_gen->getInt32(0);
        llvm::Function* fn = ctx.getFunction("aiml_io_read_i32_le", ctx.builder.getInt32Ty(), {i8p});
        llvm::Value* f = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {f}));
    };

    io.builtinFuncs["read_u32_le"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        if (args.empty()) return ctx.ir_gen->wrap(ctx.builder.getInt64(0));
        llvm::Function* fn = ctx.getFunction("aiml_io_read_u32_le", ctx.builder.getInt64Ty(), {i8p});
        llvm::Value* f = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {f}));
    };

    io.builtinFuncs["read_i64_le"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        if (args.empty()) return ctx.ir_gen->wrap(ctx.builder.getInt64(0));
        llvm::Function* fn = ctx.getFunction("aiml_io_read_i64_le", ctx.builder.getInt64Ty(), {i8p});
        llvm::Value* f = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {f}));
    };

    io.builtinFuncs["read_f32_le"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        if (args.empty()) return ctx.ir_gen->wrap(llvm::ConstantFP::get(ctx.builder.getFloatTy(), 0.0));
        llvm::Function* fn = ctx.getFunction("aiml_io_read_f32_le", ctx.builder.getFloatTy(), {i8p});
        llvm::Value* f = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {f}));
    };

    io.builtinFuncs["read_f64_le"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        if (args.empty()) return ctx.ir_gen->wrap(llvm::ConstantFP::get(ctx.builder.getDoubleTy(), 0.0));
        llvm::Function* fn = ctx.getFunction("aiml_io_read_f64_le", ctx.builder.getDoubleTy(), {i8p});
        llvm::Value* f = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {f}));
    };

    // spec/23: io.read_line(f) → one line WITHOUT the line break.  `io.read` is a block read and
    // does not stop at the end of the line (backlog item 6).  The result is an OWNED string
    // (aiml_str_dup in the runtime) and is booked as a string temp, so that a
    // consuming decl/set takes it over and an unused one is freed at the end of the statement.
    io.builtinFuncs["read_line"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        if (args.empty()) return ctx.ir_gen->wrap(ctx.builder.CreateGlobalStringPtr(""));
        llvm::Function* fn = ctx.getFunction("aiml_io_read_line", i8p, {i8p});
        llvm::Value* f = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        llvm::Value* line = ctx.builder.CreateCall(fn, {f}, "io_line");
        if (ctx.lifetimeDropsEnabled && !ctx.stmtTempStack.empty())
            AIML::AST::aimlTrackStringTemp(ctx, line);
        return ctx.ir_gen->wrap(line);
    };

    io.builtinFuncs["read_cstring"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        if (args.empty()) return ctx.ir_gen->wrap(ctx.builder.CreateGlobalStringPtr(""));
        llvm::Function* fn = ctx.getFunction("aiml_io_read_cstring", i8p, {i8p});
        llvm::Value* f = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {f}));
    };

    // io.read_bytes(f, n) → buffer<u8>  (heap AimlBinBuffer* → stack alloca via memcpy)
    io.builtinFuncs["read_bytes"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32 = ctx.builder.getInt32Ty();
        llvm::Type* i64 = ctx.builder.getInt64Ty();
        llvm::Type* sz  = ctx.getSizeType();
        llvm::Type* bt  = ctx.getLLVMType("buffer");
        if (args.size() < 2) {
            if (ctx.memV3)   // spec/61 §5: empty owned buffer handle
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(ctx.getFunction("aiml_buffer_handle_new", i8p, {}), {}, "binbuf_h"));
            auto* z = ctx.createEntryAlloca(bt, "binbuf_empty");
            ctx.builder.CreateMemSet(z, ctx.builder.getInt8(0), ctx.module().getDataLayout().getTypeAllocSize(bt), llvm::MaybeAlign());
            return ctx.ir_gen->wrap(z);
        }
        llvm::Function* fn = ctx.getFunction("aiml_io_read_bytes", i8p, {i8p, i32});
        llvm::Value* f = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        llvm::Value* n = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
        if (n->getType()->isIntegerTy(64)) n = ctx.builder.CreateTrunc(n, i32);
        llvm::Value* heapPtr = ctx.builder.CreateCall(fn, {f, n});
        llvm::Function* mcf  = ctx.getFunction("memcpy", i8p, {i8p, i8p, sz});
        uint64_t szBytes = ctx.module().getDataLayout().getTypeAllocSize(bt);
        if (ctx.memV3) {
            // spec/61 §5: the runtime's heap struct becomes an owned buffer handle (data moves along).
            llvm::Value* h = ctx.builder.CreateCall(ctx.getFunction("aiml_buffer_handle_new", i8p, {}), {}, "binbuf_alloca_h");
            ctx.builder.CreateCall(mcf, {h, heapPtr, ctx.getSizeConst(szBytes)});
            ctx.builder.CreateCall(ctx.getFunction("free", ctx.builder.getVoidTy(), {i8p}), {heapPtr});
            return ctx.ir_gen->wrap(h);
        }
        auto* out = ctx.createEntryAlloca(bt, "binbuf_alloca");
        ctx.builder.CreateCall(mcf, {out, heapPtr, ctx.getSizeConst(szBytes)});
        return ctx.ir_gen->wrap(out);
    };

    // io.read_at(f, offset, n) → buffer<u8>
    io.builtinFuncs["read_at"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32 = ctx.builder.getInt32Ty();
        llvm::Type* i64 = ctx.builder.getInt64Ty();
        llvm::Type* sz  = ctx.getSizeType();
        llvm::Type* bt  = ctx.getLLVMType("buffer");
        if (args.size() < 3) {
            if (ctx.memV3)   // spec/61 §5: empty owned buffer handle
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(ctx.getFunction("aiml_buffer_handle_new", i8p, {}), {}, "binbuf_h"));
            auto* z = ctx.createEntryAlloca(bt, "binbuf_empty");
            ctx.builder.CreateMemSet(z, ctx.builder.getInt8(0), ctx.module().getDataLayout().getTypeAllocSize(bt), llvm::MaybeAlign());
            return ctx.ir_gen->wrap(z);
        }
        llvm::Function* fn = ctx.getFunction("aiml_io_read_at", i8p, {i8p, i64, i32});
        llvm::Value* f      = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        llvm::Value* offset = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
        llvm::Value* n      = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]));
        if (offset->getType()->isIntegerTy(32)) offset = ctx.builder.CreateSExt(offset, i64);
        if (n->getType()->isIntegerTy(64))      n      = ctx.builder.CreateTrunc(n, i32);
        llvm::Value* heapPtr = ctx.builder.CreateCall(fn, {f, offset, n});
        llvm::Function* mcf  = ctx.getFunction("memcpy", i8p, {i8p, i8p, sz});
        uint64_t szBytes = ctx.module().getDataLayout().getTypeAllocSize(bt);
        if (ctx.memV3) {
            // spec/61 §5: the runtime's heap struct becomes an owned buffer handle (data moves along).
            llvm::Value* h = ctx.builder.CreateCall(ctx.getFunction("aiml_buffer_handle_new", i8p, {}), {}, "binat_alloca_h");
            ctx.builder.CreateCall(mcf, {h, heapPtr, ctx.getSizeConst(szBytes)});
            ctx.builder.CreateCall(ctx.getFunction("free", ctx.builder.getVoidTy(), {i8p}), {heapPtr});
            return ctx.ir_gen->wrap(h);
        }
        auto* out = ctx.createEntryAlloca(bt, "binat_alloca");
        ctx.builder.CreateCall(mcf, {out, heapPtr, ctx.getSizeConst(szBytes)});
        return ctx.ir_gen->wrap(out);
    };

    // io.read_f32_array(f, n) → list<f32>  (bulk single-call fread into list alloca)
    io.builtinFuncs["read_f32_array"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        llvm::Type* i8p   = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32   = ctx.builder.getInt32Ty();
        llvm::Type* voidT = llvm::Type::getVoidTy(ctx.module().getContext());
        llvm::StructType* listTy = ListImpl::getListStructType(ctx);
        // spec/61 §5: owned heap list under memV3 (caller adopts the rc).
        llvm::Value* listAlloca = ctx.memV3
            ? static_cast<llvm::Value*>(ctx.builder.CreateCall(ctx.getFunction("aiml_list_handle_new", i8p, {}), {}, "f32arr_h"))
            : ctx.createEntryAlloca(listTy, "f32arr_alloca");
        llvm::Value* nVal = (args.size() >= 2) ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])) : ctx.builder.getInt32(0);
        if (nVal->getType()->isIntegerTy(64)) nVal = ctx.builder.CreateTrunc(nVal, i32);
        ListImpl::initList(ctx, listAlloca, ctx.builder.getFloatTy(), 1);
        if (args.size() >= 2) {
            llvm::Value* fVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Function* fillFn = ctx.getFunction("aiml_io_read_f32_array_fill", voidT, {i8p, i32, i8p});
            llvm::Value* listRaw = ctx.builder.CreateBitCast(listAlloca, i8p);
            ctx.builder.CreateCall(fillFn, {fVal, nVal, listRaw});
        }
        return ctx.ir_gen->wrap(listAlloca);
    };

    // -----------------------------------------------------------------------
    // time namespace
    // -----------------------------------------------------------------------
    auto& time = ctx.namespaces["time"];

    time.builtinFuncs["now"] = [](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
        llvm::Function* f = ctx.getFunction("time_now_ms", ctx.builder.getInt64Ty(), {});
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(f));
    };

    time.builtinFuncs["mono_ms"] = [](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
        llvm::Function* f = ctx.getFunction("time_mono_ms", ctx.builder.getInt64Ty(), {});
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(f));
    };

    time.builtinFuncs["format_utc"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        if (args.empty()) return ctx.ir_gen->wrap(llvm::ConstantPointerNull::get(i8p));
        llvm::Function* f = ctx.getFunction("time_format_utc", i8p, {ctx.builder.getInt64Ty()});
        llvm::Value* ms = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        if (ms->getType()->isIntegerTy(32)) ms = ctx.builder.CreateSExt(ms, ctx.builder.getInt64Ty());
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {ms}));
    };

    time.builtinFuncs["sleep_ms"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        if (args.empty()) return ctx.ir_gen->getInt32(0);
        llvm::Function* f = ctx.getFunction("time_sleep_ms", ctx.builder.getVoidTy(), {ctx.builder.getInt32Ty()});
        llvm::Value* ms = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        if (ms->getType()->isIntegerTy(64)) ms = ctx.builder.CreateTrunc(ms, ctx.builder.getInt32Ty());
        ctx.builder.CreateCall(f, {ms});
        return ctx.ir_gen->getInt32(0);
    };

    // -----------------------------------------------------------------------
    // storage namespace — cross-platform key/value persistence (localStorage
    // on web, mirrored to ${XDG_DATA_HOME}/aiml/storage.json natively).
    // -----------------------------------------------------------------------
    auto& storage = ctx.namespaces["storage"];
    {
        auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        auto* i32 = ctx.builder.getInt32Ty();
        auto* vd  = ctx.builder.getVoidTy();

        storage.builtinFuncs["get"] = [i8p](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            if (args.empty()) return ctx.ir_gen->wrap(llvm::ConstantPointerNull::get(i8p));
            llvm::Function* f = ctx.getFunction("aiml_storage_get", i8p, {i8p});
            llvm::Value* k = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {k}));
        };

        storage.builtinFuncs["set"] = [i8p, vd](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            if (args.size() < 2) return ctx.ir_gen->getInt32(0);
            llvm::Function* f = ctx.getFunction("aiml_storage_set", vd, {i8p, i8p});
            llvm::Value* k = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            ctx.builder.CreateCall(f, {k, v});
            return ctx.ir_gen->getInt32(0);
        };

        storage.builtinFuncs["remove"] = [i8p, vd](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            if (args.empty()) return ctx.ir_gen->getInt32(0);
            llvm::Function* f = ctx.getFunction("aiml_storage_remove", vd, {i8p});
            llvm::Value* k = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            ctx.builder.CreateCall(f, {k});
            return ctx.ir_gen->getInt32(0);
        };

        storage.builtinFuncs["has"] = [i8p, i32](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            if (args.empty()) return ctx.ir_gen->getInt32(0);
            llvm::Function* f = ctx.getFunction("aiml_storage_has", i32, {i8p});
            llvm::Value* k = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {k}));
        };

        storage.builtinFuncs["clear"] = [vd](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
            llvm::Function* f = ctx.getFunction("aiml_storage_clear", vd, {});
            ctx.builder.CreateCall(f, {});
            return ctx.ir_gen->getInt32(0);
        };

        storage.builtinFuncs["host"] = [i8p](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
            llvm::Function* f = ctx.getFunction("aiml_storage_host", i8p, {});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {}));
        };
        storage.builtinFuncs["is_secure"] = [](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
            llvm::Function* f = ctx.getFunction("aiml_storage_is_secure", ctx.builder.getInt32Ty(), {});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {}));
        };
        // Session store and URL fragment (spec/23_2): sessionStorage / location.hash on WASM.
        storage.builtinFuncs["session_get"] = [i8p](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            if (args.empty()) return ctx.ir_gen->wrap(llvm::ConstantPointerNull::get(i8p));
            llvm::Function* f = ctx.getFunction("aiml_storage_session_get", i8p, {i8p});
            llvm::Value* k = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {k}));
        };
        storage.builtinFuncs["session_set"] = [i8p, vd](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            if (args.size() < 2) return ctx.ir_gen->getInt32(0);
            llvm::Function* f = ctx.getFunction("aiml_storage_session_set", vd, {i8p, i8p});
            llvm::Value* k = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            ctx.builder.CreateCall(f, {k, v});
            return ctx.ir_gen->getInt32(0);
        };
        storage.builtinFuncs["session_remove"] = [i8p, vd](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            if (args.empty()) return ctx.ir_gen->getInt32(0);
            llvm::Function* f = ctx.getFunction("aiml_storage_session_remove", vd, {i8p});
            llvm::Value* k = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            ctx.builder.CreateCall(f, {k});
            return ctx.ir_gen->getInt32(0);
        };
        storage.builtinFuncs["hash"] = [i8p](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
            llvm::Function* f = ctx.getFunction("aiml_storage_hash", i8p, {});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {}));
        };
        storage.builtinFuncs["set_hash"] = [i8p, vd](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            if (args.empty()) return ctx.ir_gen->getInt32(0);
            llvm::Function* f = ctx.getFunction("aiml_storage_set_hash", vd, {i8p});
            llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            ctx.builder.CreateCall(f, {v});
            return ctx.ir_gen->getInt32(0);
        };
    }

    // -----------------------------------------------------------------------
    // buffer builtin class
    // -----------------------------------------------------------------------
    CompilerContext::ClassInfo bufInfo;
    bufInfo.name      = "buffer";
    bufInfo.isBuiltin = true;
    // type is resolved lazily via ctx.getLLVMType("buffer") — leave nullptr here,
    // it will be filled on first getLLVMType("buffer") call.

    bufInfo.builtinMethods["alloc"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
        if (call->args.size() < 1) return ctx.ir_gen->getInt32(0);
        IRValue* sizeVal = evalExpr(call->args[0].get());
        if (sizeVal && ctx.memV3) {
            // spec/61 §5: re-allocating releases the previous data block (a fresh handle's is null)
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* dataSlot = ctx.builder.CreateStructGEP(ctx.getLLVMType("buffer"), baseAlloca, 4);
            ctx.builder.CreateCall(ctx.getFunction("free", ctx.builder.getVoidTy(), {i8p}),
                                   { ctx.builder.CreateLoad(i8p, dataSlot, "buf_old_data") });
        }
        if (sizeVal) BufferImpl::alloc(ctx, baseAlloca, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(sizeVal)));
        return ctx.ir_gen->getInt32(0);
    };

    bufInfo.builtinMethods["write"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
        if (call->args.size() < 2) return ctx.ir_gen->getInt32(0);
        IRValue* val = evalExpr(call->args[0].get());
        std::string typeName = extractTypeName(call->args[1].get());
        llvm::Type* ty = ctx.getLLVMType(typeName);
        if (!ty || !val) return ctx.ir_gen->getInt32(0);
        // spec/11: the value is written as `type` — convert a scalar of another width/kind first
        // (an i32 literal written as u8 must store one byte, not four).
        llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val));
        llvm::Type* vt = v->getType();
        bool unsignedTy = typeName == "u8" || typeName == "u16" || typeName == "u32" || typeName == "u64";
        if (vt != ty) {
            if (vt->isIntegerTy() && ty->isIntegerTy())
                v = ctx.builder.CreateSExtOrTrunc(v, ty);
            else if (vt->isIntegerTy() && ty->isFloatingPointTy())
                v = ctx.builder.CreateSIToFP(v, ty);
            else if (vt->isFloatingPointTy() && ty->isFloatingPointTy())
                v = ctx.builder.CreateFPCast(v, ty);
            else if (vt->isFloatingPointTy() && ty->isIntegerTy())
                v = unsignedTy ? ctx.builder.CreateFPToUI(v, ty) : ctx.builder.CreateFPToSI(v, ty);
        }
        BufferImpl::write(ctx, baseAlloca, v, ty);
        return ctx.ir_gen->getInt32(0);
    };

    bufInfo.builtinMethods["read"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
        if (call->args.empty()) return ctx.ir_gen->getInt32(0);
        std::string typeName = extractTypeName(call->args[0].get());
        llvm::Type* ty = ctx.getLLVMType(typeName);
        if (ty) return ctx.ir_gen->wrap(BufferImpl::read(ctx, baseAlloca, ty));
        return ctx.ir_gen->getInt32(0);
    };

    bufInfo.builtinMethods["seek"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
        if (call->args.empty()) return ctx.ir_gen->getInt32(0);
        IRValue* rawPos = evalExpr(call->args[0].get());
        if (!rawPos) return ctx.ir_gen->getInt32(0);
        llvm::Value* pos32 = ctx.builder.CreateZExtOrTrunc(
            static_cast<llvm::Value*>(ctx.ir_gen->unwrap(rawPos)), ctx.builder.getInt32Ty());
        BufferImpl::seek(ctx, baseAlloca, pos32, false);
        BufferImpl::seek(ctx, baseAlloca, pos32, true);
        return ctx.ir_gen->getInt32(0);
    };

    bufInfo.builtinMethods["len"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
        auto* sizePtr = ctx.builder.CreateStructGEP(ctx.getLLVMType("buffer"), baseAlloca, 0, "buf_len_ptr");
        return ctx.ir_gen->wrap(ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), sizePtr, "buf_len"));
    };

    // buffer.write_to(f) — spec/11: bytes [0, len) to a file handle; len, or -1 on null/short write.
    bufInfo.builtinMethods["write_to"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
        if (call->args.empty()) return ctx.ir_gen->getInt32(-1);
        IRValue* fh = evalExpr(call->args[0].get());
        if (!fh) return ctx.ir_gen->getInt32(-1);
        auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Value* handle = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(fh));
        if (handle->getType() != i8p) handle = ctx.builder.CreateBitCast(handle, i8p);
        llvm::Function* fn = ctx.getFunction("aiml_buffer_write_to", ctx.builder.getInt32Ty(), {i8p, i8p});
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {baseAlloca, handle}));
    };

    // buffer.write_string(s) — spec/11: append the string's bytes at writeIndex; count, or -1 if it does not fit.
    bufInfo.builtinMethods["write_string"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
        if (call->args.empty()) return ctx.ir_gen->getInt32(-1);
        IRValue* sv = evalExpr(call->args[0].get());
        if (!sv) return ctx.ir_gen->getInt32(-1);
        auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Value* str = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(sv));
        if (str->getType() != i8p) str = ctx.builder.CreateBitCast(str, i8p);
        llvm::Function* fn = ctx.getFunction("aiml_buffer_write_string", ctx.builder.getInt32Ty(), {i8p, i8p});
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {baseAlloca, str}));
    };

    // buffer.zero() — memset(data, 0, capacity), then size = capacity, writeIndex = capacity
    bufInfo.builtinMethods["zero"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
        auto* bufType = ctx.getLLVMType("buffer");
        // Get capacity and data pointer
        auto* capPtr = ctx.builder.CreateStructGEP(bufType, baseAlloca, 1, "buf_cap_ptr");
        auto* cap = ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), capPtr, "buf_cap");
        auto* dataPtr = ctx.builder.CreateStructGEP(bufType, baseAlloca, 4, "buf_data_ptr");
        auto* data = ctx.builder.CreateLoad(ctx.builder.getInt8Ty()->getPointerTo(), dataPtr, "buf_data");
        // memset(data, 0, capacity)
        auto* memsetFn = ctx.getFunction("memset",
            ctx.builder.getInt8Ty()->getPointerTo(),
            {ctx.builder.getInt8Ty()->getPointerTo(), ctx.builder.getInt32Ty(), ctx.getSizeType()});
        auto* capSz = ctx.toSizeType(cap);
        ctx.builder.CreateCall(memsetFn, {data, ctx.builder.getInt32(0), capSz});
        // size = capacity, writeIndex = capacity
        auto* sizePtr = ctx.builder.CreateStructGEP(bufType, baseAlloca, 0);
        ctx.builder.CreateStore(cap, sizePtr);
        auto* writePtr = ctx.builder.CreateStructGEP(bufType, baseAlloca, 3);
        ctx.builder.CreateStore(cap, writePtr);
        return ctx.ir_gen->getInt32(0);
    };

    // buffer.resize(new_size_bytes) — realloc + zero-fill new portion
    // Preserves existing data, grows buffer in-place. Updates size, cap, writeIndex.
    bufInfo.builtinMethods["resize"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
        if (call->args.empty()) return ctx.ir_gen->getInt32(0);
        IRValue* newSizeVal = evalExpr(call->args[0].get());
        if (!newSizeVal) return ctx.ir_gen->getInt32(0);
        auto* i8p     = ctx.builder.getInt8Ty()->getPointerTo();
        auto* i32     = ctx.builder.getInt32Ty();
        auto* i64     = ctx.builder.getInt64Ty();
        auto* sz      = ctx.getSizeType();  // target libc size_t
        auto* bufType = ctx.getLLVMType("buffer");
        llvm::Value* newSize = ctx.builder.CreateZExtOrTrunc(
            static_cast<llvm::Value*>(ctx.ir_gen->unwrap(newSizeVal)), i32);
        auto* sizePtr  = ctx.builder.CreateStructGEP(bufType, baseAlloca, 0);
        auto* capPtr   = ctx.builder.CreateStructGEP(bufType, baseAlloca, 1);
        auto* writePtr = ctx.builder.CreateStructGEP(bufType, baseAlloca, 3);
        auto* dataPtr  = ctx.builder.CreateStructGEP(bufType, baseAlloca, 4);
        auto* oldSize  = ctx.builder.CreateLoad(i32, sizePtr, "old_size");
        auto* oldData  = ctx.builder.CreateLoad(i8p, dataPtr, "old_data");
        auto* reallocFn = ctx.getFunction("realloc", i8p, {i8p, sz});
        auto* newSizeSz = ctx.builder.CreateZExtOrTrunc(newSize, sz);
        auto* newData   = ctx.builder.CreateCall(reallocFn, {oldData, newSizeSz}, "new_data");
        auto* oldSize64  = ctx.builder.CreateZExt(oldSize, i64);
        auto* extraStart = ctx.builder.CreateGEP(ctx.builder.getInt8Ty(), newData, oldSize64, "extra_start");
        auto* extraBytes = ctx.builder.CreateZExtOrTrunc(ctx.builder.CreateSub(newSize, oldSize), sz);
        auto* memsetFn   = ctx.getFunction("memset", i8p, {i8p, i32, sz});
        ctx.builder.CreateCall(memsetFn, {extraStart, ctx.builder.getInt32(0), extraBytes});
        ctx.builder.CreateStore(newData, dataPtr);
        ctx.builder.CreateStore(newSize, sizePtr);
        ctx.builder.CreateStore(newSize, capPtr);
        ctx.builder.CreateStore(newSize, writePtr);
        return ctx.ir_gen->getInt32(0);
    };

    bufInfo.builtinMethods["read_versioned"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
        if (call->args.empty()) return ctx.ir_gen->getInt32(0);
        std::string typeName = extractTypeName(call->args[0].get());
        llvm::Type* ty = ctx.getLLVMType(typeName);
        if (!ty) return ctx.ir_gen->getInt32(0);
        auto realEval = [&](CompilerContext& c, std::shared_ptr<AIML::AST::Expr> e) -> llvm::Value* {
            IRValue* v = evalExpr(e.get());
            if (!v) return llvm::Constant::getNullValue(ctx.getLLVMType("buffer"));
            return static_cast<llvm::Value*>(c.ir_gen->unwrap(v));
        };
        llvm::Value* explicitVer = nullptr;
        if (call->args.size() >= 2) {
            IRValue* verVal = evalExpr(call->args[1].get());
            if (verVal) explicitVer = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(verVal));
        }
        return ctx.ir_gen->wrap(BufferImpl::read_versioned(ctx, baseAlloca, ty, realEval, explicitVer));
    };

    // buffer.ws_recv(conn) → i32 — receive the next WebSocket frame straight into
    // THIS buffer (baseAlloca is the real buffer, so size/wIdx/rIdx are written
    // back).  Returns bytes received, 0 on close, -1 error, -2 overflow.
    bufInfo.builtinMethods["ws_recv"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32 = ctx.builder.getInt32Ty();
        if (call->args.empty()) return ctx.ir_gen->getInt32(-1);
        IRValue* connV = evalExpr(call->args[0].get());
        if (!connV) return ctx.ir_gen->getInt32(-1);
        llvm::Function* f = ctx.getFunction("aiml_ws_recv_binary", i32, {i8p, i8p});
        llvm::Value* bufP = ctx.builder.CreateBitCast(baseAlloca, i8p);
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
            static_cast<llvm::Value*>(ctx.ir_gen->unwrap(connV)), bufP }));
    };

    // buffer.ws_send(conn) → i32 — send THIS buffer's written bytes as one binary frame.
    bufInfo.builtinMethods["ws_send"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32 = ctx.builder.getInt32Ty();
        if (call->args.empty()) return ctx.ir_gen->getInt32(-1);
        IRValue* connV = evalExpr(call->args[0].get());
        if (!connV) return ctx.ir_gen->getInt32(-1);
        llvm::Function* f = ctx.getFunction("aiml_ws_send_binary", i32, {i8p, i8p});
        llvm::Value* bufP = ctx.builder.CreateBitCast(baseAlloca, i8p);
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
            static_cast<llvm::Value*>(ctx.ir_gen->unwrap(connV)), bufP }));
    };

    // buffer.ws_recv_append(conn) → i32 — receive the next frame APPENDED at this
    // buffer's write index (reassemble N chunk frames: alloc once, recv_append N×).
    bufInfo.builtinMethods["ws_recv_append"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32 = ctx.builder.getInt32Ty();
        if (call->args.empty()) return ctx.ir_gen->getInt32(-1);
        IRValue* connV = evalExpr(call->args[0].get());
        if (!connV) return ctx.ir_gen->getInt32(-1);
        llvm::Function* f = ctx.getFunction("aiml_ws_recv_append", i32, {i8p, i8p});
        llvm::Value* bufP = ctx.builder.CreateBitCast(baseAlloca, i8p);
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
            static_cast<llvm::Value*>(ctx.ir_gen->unwrap(connV)), bufP }));
    };

    // buffer.ws_send_range(conn, offset, len) → i32 — send data[offset..offset+len]
    // as one binary frame (zero-copy from the buffer's data pointer).  For chunked
    // uploads: slice this buffer into fixed-size frames without copying.
    bufInfo.builtinMethods["ws_send_range"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32 = ctx.builder.getInt32Ty();
        if (call->args.size() < 3) return ctx.ir_gen->getInt32(-1);
        IRValue* connV = evalExpr(call->args[0].get());
        IRValue* offV  = evalExpr(call->args[1].get());
        IRValue* lenV  = evalExpr(call->args[2].get());
        if (!connV || !offV || !lenV) return ctx.ir_gen->getInt32(-1);
        auto* bufType = ctx.getLLVMType("buffer");
        llvm::Value* dataPtrPtr = ctx.builder.CreateStructGEP(bufType, baseAlloca, 4, "buf_data_ptr");
        llvm::Value* dataPtr = ctx.builder.CreateLoad(i8p, dataPtrPtr, "buf_data");
        llvm::Value* off = ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(offV)), i32);
        llvm::Value* len = ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(lenV)), i32);
        llvm::Value* atPtr = ctx.builder.CreateInBoundsGEP(ctx.builder.getInt8Ty(), dataPtr, off, "buf_at");
        llvm::Function* f = ctx.getFunction("aiml_ws_send_raw", i32, {i8p, i8p, i32});
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
            static_cast<llvm::Value*>(ctx.ir_gen->unwrap(connV)), atPtr, len }));
    };

    // buffer.decode_image(src: buffer) → i64  — decode PNG/JPG/WebP bytes in `src`
    // into THIS buffer as tightly-packed RGBA.  Returns ((w<<32)|h), 0 on failure.
    bufInfo.builtinMethods["decode_image"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32 = ctx.builder.getInt32Ty();
        llvm::Type* i64 = ctx.builder.getInt64Ty();
        if (call->args.empty()) return ctx.ir_gen->wrap(ctx.builder.getInt64(0));
        IRValue* srcV = evalExpr(call->args[0].get());
        if (!srcV) return ctx.ir_gen->wrap(ctx.builder.getInt64(0));
        llvm::Value* srcVal  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(srcV));
        llvm::Value* srcData = ctx.builder.CreateExtractValue(srcVal, {4}, "src_data");
        llvm::Value* srcLen  = ctx.builder.CreateExtractValue(srcVal, {0}, "src_len");
        llvm::Function* f = ctx.getFunction("aiml_img_decode", i64, {i8p, i8p, i32});
        llvm::Value* dstP = ctx.builder.CreateBitCast(baseAlloca, i8p);
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {dstP, srcData, srcLen}));
    };

    // buffer.encode_webp(src: buffer, w: i32, h: i32, quality: i32) → i32 — encode the
    // RGBA bytes in `src` (w*h*4) to WebP into THIS buffer.  quality<0 = lossless.
    bufInfo.builtinMethods["encode_webp"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32 = ctx.builder.getInt32Ty();
        if (call->args.size() < 4) return ctx.ir_gen->getInt32(0);
        IRValue* srcV = evalExpr(call->args[0].get());
        IRValue* wV   = evalExpr(call->args[1].get());
        IRValue* hV   = evalExpr(call->args[2].get());
        IRValue* qV   = evalExpr(call->args[3].get());
        if (!srcV || !wV || !hV || !qV) return ctx.ir_gen->getInt32(0);
        llvm::Value* srcVal  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(srcV));
        llvm::Value* srcData = nullptr; llvm::Value* srcLen = nullptr;
        bufferArgDataLen(ctx, srcVal, srcData, srcLen);
        llvm::Value* wVal = ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(wV)), i32);
        llvm::Value* hVal = ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(hV)), i32);
        llvm::Value* qVal = ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(qV)), i32);
        llvm::Function* f = ctx.getFunction("aiml_img_encode_webp", i32, {i8p, i8p, i32, i32, i32, i32});
        llvm::Value* dstP = ctx.builder.CreateBitCast(baseAlloca, i8p);
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {dstP, srcData, srcLen, wVal, hVal, qVal}));
    };

    // buffer.encode_png(src: buffer, w: i32, h: i32) → i32 — spec/11: RGBA in `src` → PNG bytes here.
    bufInfo.builtinMethods["encode_png"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32 = ctx.builder.getInt32Ty();
        if (call->args.size() < 3) return ctx.ir_gen->getInt32(0);
        IRValue* srcV = evalExpr(call->args[0].get());
        IRValue* wV   = evalExpr(call->args[1].get());
        IRValue* hV   = evalExpr(call->args[2].get());
        if (!srcV || !wV || !hV) return ctx.ir_gen->getInt32(0);
        llvm::Value* srcVal  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(srcV));
        llvm::Value* srcData = nullptr; llvm::Value* srcLen = nullptr;
        bufferArgDataLen(ctx, srcVal, srcData, srcLen);
        llvm::Value* wVal = ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(wV)), i32);
        llvm::Value* hVal = ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(hV)), i32);
        llvm::Function* f = ctx.getFunction("aiml_img_encode_png", i32, {i8p, i8p, i32, i32, i32});
        llvm::Value* dstP = ctx.builder.CreateBitCast(baseAlloca, i8p);
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {dstP, srcData, srcLen, wVal, hVal}));
    };

    // buffer.resize_crop(src, sw, sh, dw, dh, mode) → i32 — resize RGBA in `src`
    // (sw×sh) to EXACTLY dw×dh into THIS buffer.  mode 0 = cover-crop (aspect kept),
    // 1 = stretch (fill exactly, may distort).  Enforces the canonical size.
    bufInfo.builtinMethods["resize_crop"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32 = ctx.builder.getInt32Ty();
        if (call->args.size() < 6) return ctx.ir_gen->getInt32(0);
        IRValue* srcV = evalExpr(call->args[0].get());
        IRValue* swV  = evalExpr(call->args[1].get());
        IRValue* shV  = evalExpr(call->args[2].get());
        IRValue* dwV  = evalExpr(call->args[3].get());
        IRValue* dhV  = evalExpr(call->args[4].get());
        IRValue* mdV  = evalExpr(call->args[5].get());
        if (!srcV || !swV || !shV || !dwV || !dhV || !mdV) return ctx.ir_gen->getInt32(0);
        llvm::Value* srcVal  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(srcV));
        llvm::Value* srcData = ctx.builder.CreateExtractValue(srcVal, {4}, "rc_src_data");
        llvm::Value* srcLen  = ctx.builder.CreateExtractValue(srcVal, {0}, "rc_src_len");
        auto tr = [&](IRValue* v) { return ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(v)), i32); };
        llvm::Type* f32 = ctx.builder.getFloatTy();
        llvm::Function* f = ctx.getFunction("aiml_img_resize_crop", i32, {i8p, i8p, i32, i32, i32, i32, i32, i32, f32, f32});
        llvm::Value* dstP = ctx.builder.CreateBitCast(baseAlloca, i8p);
        llvm::Value* half = llvm::ConstantFP::get(f32, 0.5);   // centered crop (API unchanged)
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {dstP, srcData, srcLen, tr(swV), tr(shV), tr(dwV), tr(dhV), tr(mdV), half, half}));
    };

    // buffer.pick_webp(dw, dh, mode) → i32 — open a file picker, convert the chosen
    // image to a dw×dh WebP and write it into THIS buffer.  mode 0 = cover-crop,
    // 1 = stretch.  Returns bytes written (0 = cancelled / error).  Native uses a
    // zenity/kdialog dialog + stb/libwebp; WASM uses a browser file input + canvas.
    bufInfo.builtinMethods["pick_webp"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32 = ctx.builder.getInt32Ty();
        if (call->args.size() < 3) return ctx.ir_gen->getInt32(0);
        IRValue* dwV = evalExpr(call->args[0].get());
        IRValue* dhV = evalExpr(call->args[1].get());
        IRValue* mdV = evalExpr(call->args[2].get());
        if (!dwV || !dhV || !mdV) return ctx.ir_gen->getInt32(0);
        auto tr = [&](IRValue* v) { return ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(v)), i32); };
        llvm::Function* f = ctx.getFunction("aiml_art_pick_webp", i32, {i8p, i32, i32, i32});
        llvm::Value* dstP = ctx.builder.CreateBitCast(baseAlloca, i8p);
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {dstP, tr(dwV), tr(dhV), tr(mdV)}));
    };

    // buffer.encode_source(dw, dh, mode, panx, pany) → i32 — crop the stashed pan
    // source (from gl.pick_source) at pan (0..1 per axis) and WebP-encode it into
    // THIS buffer.  Returns bytes written (0 = no source / error).  The final-save
    // counterpart to the live gl.source_texture preview.
    bufInfo.builtinMethods["encode_source"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32 = ctx.builder.getInt32Ty();
        llvm::Type* f32 = ctx.builder.getFloatTy();
        if (call->args.size() < 5) return ctx.ir_gen->getInt32(0);
        IRValue* dwV = evalExpr(call->args[0].get());
        IRValue* dhV = evalExpr(call->args[1].get());
        IRValue* mdV = evalExpr(call->args[2].get());
        IRValue* pxV = evalExpr(call->args[3].get());
        IRValue* pyV = evalExpr(call->args[4].get());
        if (!dwV || !dhV || !mdV || !pxV || !pyV) return ctx.ir_gen->getInt32(0);
        auto tr = [&](IRValue* v) { return ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(v)), i32); };
        auto trf = [&](IRValue* v) -> llvm::Value* {
            llvm::Value* x = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(v));
            if (x->getType()->isIntegerTy()) return ctx.builder.CreateSIToFP(x, f32);
            if (x->getType()->isDoubleTy())  return ctx.builder.CreateFPCast(x, f32);
            return x;
        };
        llvm::Function* f = ctx.getFunction("aiml_art_encode_source", i32, {i8p, i32, i32, i32, f32, f32});
        llvm::Value* dstP = ctx.builder.CreateBitCast(baseAlloca, i8p);
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {dstP, tr(dwV), tr(dhV), tr(mdV), trf(pxV), trf(pyV)}));
    };

    ctx.classes["buffer"] = std::move(bufInfo);

    // -----------------------------------------------------------------------
    // process builtin class
    // -----------------------------------------------------------------------
    CompilerContext::ClassInfo procInfo;
    procInfo.name      = "process";
    procInfo.isBuiltin = true;

    // spec/23 §5.2: members are zero-arg accessors (property reads); all null-safe in the runtime.
    // fresh = the runtime returns a NEW owned string (read_line) → statement temp the decl adopts;
    // stdout_buffer/stderr_buffer are handle-owned like a string field (readers incref).
    auto procCall = [](const char* fn, llvm::Type* (*ret)(CompilerContext&), int which, bool fresh = false) {
        return [fn, ret, which, fresh](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* procPtr = ctx.builder.CreateLoad(i8p, baseAlloca);
            std::vector<llvm::Type*> params = {i8p};
            std::vector<llvm::Value*> vals = {procPtr};
            if (which >= 0) { params.push_back(ctx.builder.getInt32Ty()); vals.push_back(ctx.builder.getInt32(which)); }
            llvm::Function* f = ctx.getFunction(fn, ret(ctx), params);
            llvm::Value* r = ctx.builder.CreateCall(f, vals);
            if (r->getType()->isVoidTy()) return ctx.ir_gen->getInt32(0);
            if (r->getType()->isIntegerTy(1)) r = ctx.builder.CreateZExt(r, ctx.builder.getInt32Ty());   // bool as i32 0/1 (like io.is_file)
            if (fresh && ctx.lifetimeDropsEnabled && !ctx.stmtTempStack.empty()) AIML::AST::aimlTrackStringTemp(ctx, r);
            return ctx.ir_gen->wrap(r);
        };
    };
    auto rI32  = [](CompilerContext& c) -> llvm::Type* { return c.builder.getInt32Ty(); };
    auto rI64  = [](CompilerContext& c) -> llvm::Type* { return c.builder.getInt64Ty(); };
    auto rBool = [](CompilerContext& c) -> llvm::Type* { return c.builder.getInt1Ty(); };
    auto rStr  = [](CompilerContext& c) -> llvm::Type* { return c.builder.getInt8Ty()->getPointerTo(); };
    auto rVoid = [](CompilerContext& c) -> llvm::Type* { return c.builder.getVoidTy(); };
    procInfo.builtinMethods["wait"]          = procCall("process_wait", rI32, -1);
    procInfo.builtinMethods["kill"]          = procCall("process_kill", rVoid, -1);
    procInfo.builtinMethods["running"]       = procCall("aiml_process_running", rBool, -1);
    // spec/23 §5.2: proc.signal(sig) → i32 (0 sent, -1 not)
    procInfo.builtinMethods["signal"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32 = ctx.builder.getInt32Ty();
        llvm::Value* sig = ctx.builder.getInt32(15);
        if (call && !call->args.empty())
            if (IRValue* a = evalExpr(call->args[0].get())) sig = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a));
        if (sig->getType()->isIntegerTy() && sig->getType() != i32) sig = ctx.builder.CreateIntCast(sig, i32, true);
        llvm::Value* procPtr = ctx.builder.CreateLoad(i8p, baseAlloca);
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(ctx.getFunction("aiml_process_signal", i32, {i8p, i32}), {procPtr, sig}));
    };

    procInfo.builtinMethods["eof"]           = procCall("aiml_process_eof", rBool, -1);
    procInfo.builtinMethods["pid"]           = procCall("aiml_process_pid", rI64, -1);
    procInfo.builtinMethods["returncode"]    = procCall("aiml_process_returncode", rI32, -1);
    procInfo.builtinMethods["stdout_buffer"] = procCall("aiml_process_buffer", rStr, 0);
    procInfo.builtinMethods["stderr_buffer"] = procCall("aiml_process_buffer", rStr, 1);
    procInfo.builtinMethods["read_line"]     = procCall("aiml_process_read_line", rStr, 0, true);
    procInfo.builtinMethods["read_err_line"] = procCall("aiml_process_read_line", rStr, 1, true);

    ctx.classes["process"] = std::move(procInfo);

    // -----------------------------------------------------------------------
    // zim_handle — an open ZIM archive (spec/33_2 §6).  An opaque pointer like `process`;
    // every member below is a zero-arg accessor, so `zim.entry_count` reads as a property.
    //
    // A call that can fail records its reason in the runtime and the guard here turns that
    // into the ZimError of §6.4 (message + code), so no libzim exception ever crosses into
    // generated code.  A LOOKUP that finds nothing is not a failure: it sets found = 0.
    // -----------------------------------------------------------------------
    {
        CompilerContext::ClassInfo zimInfo;
        zimInfo.name      = "zim_handle";
        zimInfo.isBuiltin = true;

        // The entry argument as a POINTER: the runtime reads an AimlZimEntry*.  A local
        // arrives as a loaded struct (spill it), a member or element as a pointer already.
        auto entryArg = [](CompilerContext& ctx, AIML::AST::CallExpr* call,
                           std::function<IRValue*(AIML::AST::Expr*)> evalExpr,
                           size_t argNo) -> llvm::Value* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            if (!call || call->args.size() <= argNo) return llvm::ConstantPointerNull::get(p);
            IRValue* a = evalExpr(call->args[argNo].get());
            if (!a) return llvm::ConstantPointerNull::get(p);
            llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a));
            if (!v) return llvm::ConstantPointerNull::get(p);
            if (v->getType()->isPointerTy()) return v;
            llvm::Type* entryTy = ctx.getLLVMType("zim_entry");
            llvm::Value* tmp = ctx.createEntryAlloca(entryTy, "zim_entry_arg");
            ctx.builder.CreateStore(v, tmp);
            return tmp;
        };

        // After a fallible call: throw ZimError(message, code) when the runtime set a code.
        auto guardZim = [](CompilerContext& ctx) {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Value* code = ctx.builder.CreateCall(
                ctx.getFunction("aiml_zim_last_code", i32, {}), {}, "zim_code");
            llvm::Value* bad = ctx.builder.CreateICmpNE(code, ctx.builder.getInt32(0), "zim_bad");
            llvm::Value* msg = ctx.builder.CreateCall(
                ctx.getFunction("aiml_zim_last_message", p, {}), {}, "zim_msg");
            emitGuardMsgCode(ctx, ctx.builder.CreateZExt(bad, ctx.builder.getInt8Ty()),
                             "zim_throw", "zim_ok", "ZimError", msg, code);
        };

        auto handleOf = [](CompilerContext& ctx, llvm::Value* baseAlloca) -> llvm::Value* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            return ctx.builder.CreateLoad(p, baseAlloca, "zim_h");
        };

        // zim.entry_count / zim.article_count → i64
        auto countFn = [handleOf](const char* rtName) {
            return [handleOf, rtName](CompilerContext& ctx, llvm::Value* baseAlloca,
                AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
                auto* p = llvm::PointerType::get(ctx.context, 0);
                llvm::Type* i64 = ctx.builder.getInt64Ty();
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction(rtName, i64, {p}), {handleOf(ctx, baseAlloca)}));
            };
        };
        zimInfo.builtinMethods["entry_count"]   = countFn("aiml_zim_entry_count");
        zimInfo.builtinMethods["article_count"] = countFn("aiml_zim_article_count");
        // §6.3: how far a scan reaches.  NOT entry_count — that one also counts the metadata
        // and internal entries (Title, Language, the counter, the title index), which have no
        // place in the cluster order.
        zimInfo.builtinMethods["scan_count"]    = countFn("aiml_zim_scan_count");

        // zim.uuid → string (32 hex digits)
        zimInfo.builtinMethods["uuid"] = [handleOf](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            return ownedStringResult(ctx, ctx.builder.CreateCall(
                ctx.getFunction("aiml_zim_uuid", p, {p}), {handleOf(ctx, baseAlloca)}));
        };

        // zim.metadata(key) → string ("" when the key is absent)
        zimInfo.builtinMethods["metadata"] = [handleOf](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* key = llvm::ConstantPointerNull::get(p);
            if (call && !call->args.empty())
                if (IRValue* a = evalExpr(call->args[0].get()))
                    key = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a));
            return ownedStringResult(ctx, ctx.builder.CreateCall(
                ctx.getFunction("aiml_zim_metadata", p, {p, p}), {handleOf(ctx, baseAlloca), key}));
        };

        // zim.checksum_ok → bool (reads every byte of the file)
        zimInfo.builtinMethods["checksum_ok"] = [handleOf](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_zim_checksum_ok", i32, {p}), {handleOf(ctx, baseAlloca)}));
        };

        // zim.close
        zimInfo.builtinMethods["close"] = [handleOf](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* h = handleOf(ctx, baseAlloca);
            ctx.builder.CreateCall(ctx.getFunction("aiml_zim_close", ctx.builder.getVoidTy(), {p}), {h});
            // null the slot so a scope-drop (or a second close) cannot free it twice
            ctx.builder.CreateStore(llvm::ConstantPointerNull::get(p), baseAlloca);
            return ctx.ir_gen->getInt32(0);
        };

        // zim.by_path(path) / zim.by_title(title) → zim_entry
        auto lookupFn = [handleOf](const char* rtName) {
            return [handleOf, rtName](CompilerContext& ctx, llvm::Value* baseAlloca,
                AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
                auto* p = llvm::PointerType::get(ctx.context, 0);
                llvm::Value* key = llvm::ConstantPointerNull::get(p);
                if (call && !call->args.empty())
                    if (IRValue* a = evalExpr(call->args[0].get()))
                        key = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a));
                llvm::Type* entryTy = ctx.getLLVMType("zim_entry");
                llvm::Value* out = ctx.createEntryAlloca(entryTy, "zim_lookup");
                ctx.builder.CreateCall(
                    ctx.getFunction(rtName, ctx.builder.getVoidTy(), {p, p, p}),
                    {handleOf(ctx, baseAlloca), key, out});
                return ctx.ir_gen->wrap(ctx.builder.CreateLoad(entryTy, out, "zim_entry_val"));
            };
        };
        zimInfo.builtinMethods["by_path"]  = lookupFn("aiml_zim_by_path");
        zimInfo.builtinMethods["by_title"] = lookupFn("aiml_zim_by_title");

        // zim.resolve(entry) → zim_entry; a loop or a chain over 16 hops throws
        zimInfo.builtinMethods["resolve"] = [handleOf, entryArg, guardZim](CompilerContext& ctx,
            llvm::Value* baseAlloca, AIML::AST::CallExpr* call,
            std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Type* entryTy = ctx.getLLVMType("zim_entry");
            llvm::Value* in  = entryArg(ctx, call, evalExpr, 0);
            llvm::Value* out = ctx.createEntryAlloca(entryTy, "zim_resolved");
            ctx.builder.CreateCall(ctx.getFunction("aiml_zim_resolve", i32, {p, p, p}),
                                   {handleOf(ctx, baseAlloca), in, out});
            guardZim(ctx);
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(entryTy, out, "zim_resolved_val"));
        };

        // zim.path / zim.title / zim.mimetype / zim.redirect_path (entry) → string
        auto strFn = [handleOf, entryArg](int which) {
            return [handleOf, entryArg, which](CompilerContext& ctx, llvm::Value* baseAlloca,
                AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
                auto* p = llvm::PointerType::get(ctx.context, 0);
                llvm::Type* i32 = ctx.builder.getInt32Ty();
                llvm::Value* e = entryArg(ctx, call, evalExpr, 0);
                return ownedStringResult(ctx, ctx.builder.CreateCall(
                    ctx.getFunction("aiml_zim_str", p, {p, p, i32}),
                    {handleOf(ctx, baseAlloca), e, ctx.builder.getInt32(which)}));
            };
        };
        zimInfo.builtinMethods["path"]          = strFn(0);
        zimInfo.builtinMethods["title"]         = strFn(1);
        zimInfo.builtinMethods["mimetype"]      = strFn(2);
        zimInfo.builtinMethods["redirect_path"] = strFn(3);

        // zim.content(entry) → buffer<u8>; a redirect or a missing entry throws
        zimInfo.builtinMethods["content"] = [handleOf, entryArg, guardZim](CompilerContext& ctx,
            llvm::Value* baseAlloca, AIML::AST::CallExpr* call,
            std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* e = entryArg(ctx, call, evalExpr, 0);
            llvm::Value* heap = ctx.builder.CreateCall(
                ctx.getFunction("aiml_zim_content", p, {p, p}), {handleOf(ctx, baseAlloca), e});
            guardZim(ctx);
            // Copy the heap AimlCompressBuffer into an owned buffer handle and free it — the
            // same hand-over compress.gzip does (a stack buffer would be "released" by the
            // local's drop, i.e. free() on stack memory).
            llvm::Type* bufTy = ctx.getLLVMType("buffer");
            llvm::Value* out = ctx.memV3
                ? static_cast<llvm::Value*>(ctx.builder.CreateCall(
                      ctx.getFunction("aiml_buffer_handle_new", p, {}), {}, "zim_buf_h"))
                : static_cast<llvm::Value*>(ctx.createEntryAlloca(bufTy, "zim_buf"));
            uint64_t bufSz = ctx.module().getDataLayout().getTypeAllocSize(bufTy);
            llvm::Function* cur = ctx.builder.GetInsertBlock()->getParent();
            auto* copyBB = llvm::BasicBlock::Create(ctx.context, "zim_buf_copy", cur);
            auto* doneBB = llvm::BasicBlock::Create(ctx.context, "zim_buf_done", cur);
            ctx.builder.CreateCondBr(ctx.builder.CreateIsNull(heap), doneBB, copyBB);
            ctx.builder.SetInsertPoint(copyBB);
            ctx.builder.CreateCall(
                ctx.getFunction("memcpy", p, {p, p, ctx.getSizeType()}),
                {out, heap, ctx.getSizeConst(bufSz)});
            ctx.builder.CreateCall(ctx.getFunction("free", ctx.builder.getVoidTy(), {p}), {heap});
            ctx.builder.CreateBr(doneBB);
            ctx.builder.SetInsertPoint(doneBB);
            return ctx.ir_gen->wrap(out);
        };

        // zim.content_str(entry) → string; same errors as content
        zimInfo.builtinMethods["content_str"] = [handleOf, entryArg, guardZim](CompilerContext& ctx,
            llvm::Value* baseAlloca, AIML::AST::CallExpr* call,
            std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* e = entryArg(ctx, call, evalExpr, 0);
            llvm::Value* str = ctx.builder.CreateCall(
                ctx.getFunction("aiml_zim_content_str", p, {p, p}), {handleOf(ctx, baseAlloca), e});
            guardZim(ctx);
            return ownedStringResult(ctx, str);
        };

        // zim.scan(from, count) → list<zim_entry>, in cluster order
        zimInfo.builtinMethods["scan"] = [handleOf](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Type* i64 = ctx.builder.getInt64Ty();
            llvm::Type* entryTy = ctx.getLLVMType("zim_entry");
            auto num = [&](size_t idx, llvm::Type* want, int64_t dflt) -> llvm::Value* {
                if (call && call->args.size() > idx)
                    if (IRValue* a = evalExpr(call->args[idx].get())) {
                        llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a));
                        if (v && v->getType()->isIntegerTy()) return ctx.builder.CreateIntCast(v, want, true);
                        if (v && v->getType()->isFloatingPointTy()) return ctx.builder.CreateFPToSI(v, want);
                    }
                return llvm::ConstantInt::get(want, dflt);
            };
            llvm::Value* from  = num(0, i64, 0);
            llvm::Value* count = num(1, i32, 1000);
            llvm::StructType* listTy = ListImpl::getListStructType(ctx);
            llvm::Value* list = ctx.memV3
                ? static_cast<llvm::Value*>(ctx.builder.CreateCall(
                      ctx.getFunction("aiml_list_handle_new", p, {}), {}, "zim_scan_h"))
                : static_cast<llvm::Value*>(ctx.createEntryAlloca(listTy, "zim_scan_list"));
            ListImpl::initList(ctx, list, entryTy, 8);
            ctx.builder.CreateCall(ctx.getFunction("aiml_zim_scan", i32, {p, i64, i32, p}),
                                   {handleOf(ctx, baseAlloca), from, count, list});
            return ctx.ir_gen->wrap(list);
        };

        // zim.position(entry) → i64; just the field the scan wrote, -1 for a lookup
        zimInfo.builtinMethods["position"] = [entryArg](CompilerContext& ctx, llvm::Value*,
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
            llvm::Type* entryTy = ctx.getLLVMType("zim_entry");
            llvm::Value* e = entryArg(ctx, call, evalExpr, 0);
            auto* st = llvm::cast<llvm::StructType>(entryTy);
            llvm::Value* gep = ctx.builder.CreateStructGEP(st, e, 2, "zim_pos_gep");
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(ctx.builder.getInt64Ty(), gep, "zim_pos"));
        };

        ctx.classes["zim_handle"] = std::move(zimInfo);
    }

    // -----------------------------------------------------------------------
    // thread_handle builtin class: { i64 tid, i8 joined, i8 detached }
    // Stored as thread_handle* (pointer to heap-allocated struct).
    // -----------------------------------------------------------------------
    llvm::StructType* handleTy = llvm::StructType::getTypeByName(ctx.context, "thread_handle");
    if (!handleTy) {
        handleTy = llvm::StructType::create(ctx.context,
            {ctx.builder.getInt64Ty(), ctx.builder.getInt8Ty(), ctx.builder.getInt8Ty()},
            "thread_handle");
    }

    CompilerContext::ClassInfo threadInfo;
    threadInfo.name      = "thread_handle";
    threadInfo.isBuiltin = true;
    threadInfo.type      = handleTy;

    // Member "id" = tid at struct index 0
    CompilerContext::StructMember idMember;
    idMember.typeName = "i64";
    idMember.type     = ctx.builder.getInt64Ty();
    idMember.index    = 0;
    idMember.access   = AccessLevel::Public;
    threadInfo.members["id"] = idMember;

    threadInfo.builtinMethods["join"] = [handleTy](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
        llvm::Type* handlePtrTy = handleTy->getPointerTo();
        llvm::Value* handlePtr  = ctx.builder.CreateLoad(handlePtrTy, baseAlloca, "handle_ptr");

        // Guard: throw InvalidThreadStateException if already detached
        llvm::Value* detachedGEP = ctx.builder.CreateStructGEP(handleTy, handlePtr, 2, "detached_ptr");
        llvm::Value* detachedVal = ctx.builder.CreateLoad(ctx.builder.getInt8Ty(), detachedGEP, "detached");
        emitGuard(ctx, detachedVal, "join_detached_throw", "join_check_joined", "InvalidThreadStateException");

        // Guard: throw InvalidThreadStateException if already joined
        llvm::Value* joinedGEP = ctx.builder.CreateStructGEP(handleTy, handlePtr, 1, "joined_ptr");
        llvm::Value* joinedVal = ctx.builder.CreateLoad(ctx.builder.getInt8Ty(), joinedGEP, "joined");
        emitGuard(ctx, joinedVal, "join_already_throw", "join_do", "InvalidThreadStateException");

        // pthread_join(tid, NULL)
        llvm::Value* tidGEP = ctx.builder.CreateStructGEP(handleTy, handlePtr, 0, "tid_ptr");
        llvm::Value* tid    = ctx.builder.CreateLoad(ctx.builder.getInt64Ty(), tidGEP, "tid");
        llvm::Type* i8pp = ctx.builder.getInt8Ty()->getPointerTo()->getPointerTo();
        llvm::Function* f = ctx.getFunction("pthread_join",
            ctx.builder.getInt32Ty(), {ctx.getSizeType(), i8pp});
        ctx.builder.CreateCall(f, {ctx.toSizeType(tid),
            llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8pp))});
        ctx.builder.CreateStore(ctx.builder.getInt8(1), joinedGEP);
        return ctx.ir_gen->getInt32(0);
    };

    threadInfo.builtinMethods["detach"] = [handleTy](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
        llvm::Type* handlePtrTy = handleTy->getPointerTo();
        llvm::Value* handlePtr  = ctx.builder.CreateLoad(handlePtrTy, baseAlloca, "handle_ptr");

        // Guard: throw InvalidThreadStateException if already joined
        llvm::Value* joinedGEP = ctx.builder.CreateStructGEP(handleTy, handlePtr, 1, "joined_ptr");
        llvm::Value* joinedVal = ctx.builder.CreateLoad(ctx.builder.getInt8Ty(), joinedGEP, "joined");
        emitGuard(ctx, joinedVal, "detach_joined_throw", "detach_check_detached", "InvalidThreadStateException");

        // Guard: throw InvalidThreadStateException if already detached
        llvm::Value* detachedGEP = ctx.builder.CreateStructGEP(handleTy, handlePtr, 2, "detached_ptr");
        llvm::Value* detachedVal = ctx.builder.CreateLoad(ctx.builder.getInt8Ty(), detachedGEP, "detached");
        emitGuard(ctx, detachedVal, "detach_already_throw", "detach_do", "InvalidThreadStateException");

        // pthread_detach(tid)
        llvm::Value* tidGEP = ctx.builder.CreateStructGEP(handleTy, handlePtr, 0, "tid_ptr");
        llvm::Value* tid    = ctx.builder.CreateLoad(ctx.builder.getInt64Ty(), tidGEP, "tid");
        llvm::Function* f = ctx.getFunction("pthread_detach",
            ctx.builder.getInt32Ty(), {ctx.getSizeType()});
        ctx.builder.CreateCall(f, {ctx.toSizeType(tid)});
        ctx.builder.CreateStore(ctx.builder.getInt8(1), detachedGEP);
        return ctx.ir_gen->getInt32(0);
    };

    ctx.classes["thread_handle"] = std::move(threadInfo);

    // -----------------------------------------------------------------------
    // lock builtin class: opaque i8* pointing to heap-allocated pthread_mutex_t
    // sizeof(pthread_mutex_t) == 40 on Linux x86_64
    // -----------------------------------------------------------------------
    CompilerContext::ClassInfo lockInfo;
    lockInfo.name      = "lock";
    lockInfo.isBuiltin = true;

    lockInfo.builtinMethods["init"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Function* mallocFn = ctx.getFunction("malloc", i8p, {ctx.getSizeType()});
        llvm::Value* mutexMem = ctx.builder.CreateCall(mallocFn,
            {ctx.getSizeConst(40)}, "mutex_mem");
        ctx.builder.CreateStore(mutexMem, baseAlloca);
        llvm::Function* initFn = ctx.getFunction("pthread_mutex_init",
            ctx.builder.getInt32Ty(), {i8p, i8p});
        ctx.builder.CreateCall(initFn, {mutexMem,
            llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p))});
        return ctx.ir_gen->getInt32(0);
    };

    lockInfo.builtinMethods["acquire"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Value* mutexPtr = ctx.builder.CreateLoad(i8p, baseAlloca, "mutex_ptr");
        llvm::Function* f = ctx.getFunction("pthread_mutex_lock",
            ctx.builder.getInt32Ty(), {i8p});
        ctx.builder.CreateCall(f, {mutexPtr});
        return ctx.ir_gen->getInt32(0);
    };

    lockInfo.builtinMethods["release"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Value* mutexPtr = ctx.builder.CreateLoad(i8p, baseAlloca, "mutex_ptr");
        llvm::Function* f = ctx.getFunction("pthread_mutex_unlock",
            ctx.builder.getInt32Ty(), {i8p});
        ctx.builder.CreateCall(f, {mutexPtr});
        return ctx.ir_gen->getInt32(0);
    };

    ctx.classes["lock"] = std::move(lockInfo);

    // -----------------------------------------------------------------------
    // Code builtin class: opaque i8* → heap-allocated __aiml_code_t
    // { id: i8*, source: i8*, src_len: i64, entry: i8* }
    // -----------------------------------------------------------------------
    CompilerContext::ClassInfo codeInfo;
    codeInfo.name      = "Code";
    codeInfo.isBuiltin = true;

    codeInfo.builtinMethods["contains"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Value* codePtr = ctx.builder.CreateLoad(i8p, baseAlloca, "code_ptr");
        if (call->args.empty()) return ctx.ir_gen->wrap(ctx.builder.getInt8(0));
        IRValue* nameVal = evalExpr(call->args[0].get());
        if (!nameVal) return ctx.ir_gen->wrap(ctx.builder.getInt8(0));
        llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(nameVal));
        return ctx.ir_gen->wrap(CodeImpl::contains(ctx, codePtr, name));
    };

    codeInfo.builtinMethods["size"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Value* codePtr = ctx.builder.CreateLoad(i8p, baseAlloca, "code_ptr");
        return ctx.ir_gen->wrap(CodeImpl::size(ctx, codePtr));
    };

    codeInfo.builtinMethods["toString"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Value* codePtr = ctx.builder.CreateLoad(i8p, baseAlloca, "code_ptr");
        return ctx.ir_gen->wrap(CodeImpl::toString(ctx, codePtr));
    };

    codeInfo.builtinMethods["merge"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Value* aPtr = ctx.builder.CreateLoad(i8p, baseAlloca, "code_a");
        if (call->args.empty()) return ctx.ir_gen->wrap(aPtr);
        IRValue* bVal = evalExpr(call->args[0].get());
        if (!bVal) return ctx.ir_gen->wrap(aPtr);
        llvm::Value* bPtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(bVal));
        return ctx.ir_gen->wrap(CodeImpl::merge(ctx, aPtr, bPtr));
    };

    ctx.classes["Code"] = std::move(codeInfo);

    // -----------------------------------------------------------------------
    // spec/62 §13.5: step / stage / pipeline — composable pipelines.
    // Opaque i8* handles like Code/process/path; the structure grows, so
    // no struct carries it.  The BODY of a step is a code bundle
    // (F62-85), so that the purity check and the content-derived key
    // are preserved.
    // -----------------------------------------------------------------------
    {
        auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        auto* i32 = ctx.builder.getInt32Ty();
        auto* vd  = ctx.builder.getVoidTy();

        // Helper lambdas: method with (string) / (i32) / (handle) / () signature.
        // An argument that could not be compiled is nullptr — the
        // error is then already in the log.  Computing on would mean
        // calling `getType()` on nullptr: the compiler dies on the
        // input it is currently complaining about (measured 2026-10-25 with
        // `cache: haeufig`).  So: stop here.
        auto argOrNull = [](CompilerContext& ctx, AIML::IRValue* rv,
                            const char* what) -> llvm::Value* {
            llvm::Value* got = rv ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(rv)) : nullptr;
            if (!got) LOG_ERROR(std::string(what) + ": the argument could not be compiled");
            return got;
        };
        auto mkStr = [argOrNull](const char* cfn) {
            return [cfn, argOrNull](CompilerContext& ctx, llvm::Value* base,
                         AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                if (call->args.empty()) return nullptr;
                llvm::Value* arg = argOrNull(ctx, evalExpr(call->args[0].get()), cfn);
                if (!arg) return nullptr;
                llvm::Value* self = ctx.builder.CreateLoad(i8p, base, "self");
                ctx.builder.CreateCall(ctx.getFunction(cfn, ctx.builder.getVoidTy(), {i8p, i8p}), {self, arg});
                return nullptr;
            };
        };
        auto mkI32 = [argOrNull](const char* cfn) {
            return [cfn, argOrNull](CompilerContext& ctx, llvm::Value* base,
                         AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                auto* i32 = ctx.builder.getInt32Ty();
                if (call->args.empty()) return nullptr;
                llvm::Value* arg = argOrNull(ctx, evalExpr(call->args[0].get()), cfn);
                if (!arg) return nullptr;
                llvm::Value* self = ctx.builder.CreateLoad(i8p, base, "self");
                if (arg->getType() != i32) arg = ctx.builder.CreateZExtOrTrunc(arg, i32);
                ctx.builder.CreateCall(ctx.getFunction(cfn, ctx.builder.getVoidTy(), {i8p, i32}), {self, arg});
                return nullptr;
            };
        };
        auto mkHandle = [argOrNull](const char* cfn) {
            return [cfn, argOrNull](CompilerContext& ctx, llvm::Value* base,
                         AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                if (call->args.empty()) return nullptr;
                llvm::Value* arg = argOrNull(ctx, evalExpr(call->args[0].get()), cfn);
                if (!arg) return nullptr;
                llvm::Value* self = ctx.builder.CreateLoad(i8p, base, "self");
                ctx.builder.CreateCall(ctx.getFunction(cfn, ctx.builder.getVoidTy(), {i8p, i8p}), {self, arg});
                return nullptr;
            };
        };
        auto mkGetI32 = [](const char* cfn) {
            return [cfn](CompilerContext& ctx, llvm::Value* base,
                         AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                (void)call; (void)evalExpr;
                auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                auto* i32 = ctx.builder.getInt32Ty();
                llvm::Value* self = ctx.builder.CreateLoad(i8p, base, "self");
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction(cfn, i32, {i8p}), {self}));
            };
        };

        // ── spec/62 §20.4: the READERS ──────────────────────────────────────
        //
        // One named reader per field, which brings the key name along — that way
        // the name is ONCE in the spec, once in the rewrite and
        // once here, and a typo is a compile error instead of
        // an empty string.
        auto mkGetStr = [](const char* cfn) {
            return [cfn](CompilerContext& ctx, llvm::Value* base,
                         AIML::AST::CallExpr*, auto) -> IRValue* {
                auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Value* self = ctx.builder.CreateLoad(i8p, base, "self");
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction(cfn, i8p, {i8p}), {self}));
            };
        };
        // Reader with a fixed key: `stg.agent` calls attr(self, "agent").
        auto mkAttrStr = [](const char* cfn, const char* key) {
            return [cfn, key](CompilerContext& ctx, llvm::Value* base,
                              AIML::AST::CallExpr*, auto) -> IRValue* {
                auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Value* self = ctx.builder.CreateLoad(i8p, base, "self");
                llvm::Value* kv = ctx.builder.CreateGlobalStringPtr(key, "attr_key");
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction(cfn, i8p, {i8p, i8p}), {self, kv}));
            };
        };
        auto mkAttrI32 = [](const char* cfn, const char* key) {
            return [cfn, key](CompilerContext& ctx, llvm::Value* base,
                              AIML::AST::CallExpr*, auto) -> IRValue* {
                auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                auto* i32 = ctx.builder.getInt32Ty();
                llvm::Value* self = ctx.builder.CreateLoad(i8p, base, "self");
                llvm::Value* kv = ctx.builder.CreateGlobalStringPtr(key, "limit_key");
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction(cfn, i32, {i8p, i8p}), {self, kv}));
            };
        };
        // (i32) -> string, z.B. pipe.lane_bindings(2)
        auto mkIdxStr = [](const char* cfn) {
            return [cfn](CompilerContext& ctx, llvm::Value* base,
                         AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                auto* i32 = ctx.builder.getInt32Ty();
                if (call->args.empty()) return nullptr;
                llvm::Value* self = ctx.builder.CreateLoad(i8p, base, "self");
                llvm::Value* idx = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
                if (idx->getType() != i32) idx = ctx.builder.CreateZExtOrTrunc(idx, i32);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction(cfn, i8p, {i8p, i32}), {self, idx}));
            };
        };
        auto mkIdxI32 = [](const char* cfn) {
            return [cfn](CompilerContext& ctx, llvm::Value* base,
                         AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                auto* i32 = ctx.builder.getInt32Ty();
                if (call->args.empty()) return nullptr;
                llvm::Value* self = ctx.builder.CreateLoad(i8p, base, "self");
                llvm::Value* idx = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
                if (idx->getType() != i32) idx = ctx.builder.CreateZExtOrTrunc(idx, i32);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction(cfn, i32, {i8p, i32}), {self, idx}));
            };
        };
        // (i32, i32) -> string, z.B. pipe.axis_value(0, 1)
        auto mkIdx2Str = [](const char* cfn) {
            return [cfn](CompilerContext& ctx, llvm::Value* base,
                         AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                auto* i32 = ctx.builder.getInt32Ty();
                if (call->args.size() < 2) return nullptr;
                llvm::Value* self = ctx.builder.CreateLoad(i8p, base, "self");
                llvm::Value* one = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
                llvm::Value* two = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
                if (one->getType() != i32) one = ctx.builder.CreateZExtOrTrunc(one, i32);
                if (two->getType() != i32) two = ctx.builder.CreateZExtOrTrunc(two, i32);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction(cfn, i8p, {i8p, i32, i32}), {self, one, two}));
            };
        };
        // (i32) -> string mit festem Zweitschluessel: pipe.push_from(0).
        auto mkIdxKeyStr = [](const char* cfn, const char* key) {
            return [cfn, key](CompilerContext& ctx, llvm::Value* base,
                              AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                auto* i32 = ctx.builder.getInt32Ty();
                if (call->args.empty()) return nullptr;
                llvm::Value* self = ctx.builder.CreateLoad(i8p, base, "self");
                llvm::Value* idx = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
                if (idx->getType() != i32) idx = ctx.builder.CreateZExtOrTrunc(idx, i32);
                llvm::Value* kv = ctx.builder.CreateGlobalStringPtr(key, "push_key");
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction(cfn, i8p, {i8p, i32, i8p}), {self, idx, kv}));
            };
        };
        // (i32) -> handle (stage/step).  nullptr means: index out of range — and that
        // is a programming error of the reader, so it throws (F62-154).
        auto mkIdxHandle = [](const char* cfn, const char* what) {
            return [cfn, what](CompilerContext& ctx, llvm::Value* base,
                               AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                auto* i32 = ctx.builder.getInt32Ty();
                if (call->args.empty()) return nullptr;
                llvm::Value* self = ctx.builder.CreateLoad(i8p, base, "self");
                llvm::Value* idx = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
                if (idx->getType() != i32) idx = ctx.builder.CreateZExtOrTrunc(idx, i32);
                llvm::Value* got = ctx.builder.CreateCall(
                    ctx.getFunction(cfn, i8p, {i8p, i32}), {self, idx});
                llvm::Value* isNull = ctx.builder.CreateICmpEQ(
                    got, llvm::ConstantPointerNull::get(llvm::PointerType::getUnqual(ctx.context)));
                llvm::Value* flag = ctx.builder.CreateZExt(isNull, ctx.builder.getInt8Ty());
                llvm::Value* msg = ctx.builder.CreateGlobalStringPtr(
                    std::string(what) + ": index out of range (spec/62 F62-154)", "idx_msg");
                emitGuardMsg(ctx, flag, "idx_bad", "idx_ok", "IndexOutOfBoundsException", msg);
                return ctx.ir_gen->wrap(got);
            };
        };

        CompilerContext::ClassInfo stepInfo;
        stepInfo.name = "step"; stepInfo.isBuiltin = true;
        stepInfo.builtinMethods["set_name"]  = mkStr("aiml_step_set_name");
        stepInfo.builtinMethods["set_body"]  = mkHandle("aiml_step_set_body");
        stepInfo.builtinMethods["set_cache"] = mkI32("aiml_step_set_cache");
        stepInfo.builtinMethods["use_arg"]   = mkStr("aiml_step_use_arg");
        // spec/62 §5.6 — reach, time limit, attempts (not key material, F62-91).
        stepInfo.builtinMethods["set_on_fail"] = mkI32("aiml_step_set_on_fail");
        stepInfo.builtinMethods["set_timeout"] = mkI32("aiml_step_set_timeout");
        stepInfo.builtinMethods["set_retry"]   = mkI32("aiml_step_set_retry");
        stepInfo.builtinMethods["status"]      = mkGetI32("aiml_step_status");
        stepInfo.builtinMethods["set_pause_before"] = mkI32("aiml_step_set_pause_before");
        // spec/62 §5.5 / §13.5g
        stepInfo.builtinMethods["set_per"]   = mkI32("aiml_step_set_per");
        stepInfo.builtinMethods["add_need"]  = mkStr("aiml_step_add_need");
        // spec/62 §9: set_resolve(fn) — fn is a function name, we need
        // its address (as with pipeline.run(observer)).
        stepInfo.builtinMethods["set_resolve"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            (void)evalExpr;
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            if (call->args.empty()) return nullptr;
            llvm::Value* self = ctx.builder.CreateLoad(i8p, base, "self");
            llvm::Value* fnPtr = nullptr;
            if (auto* var = dynamic_cast<AIML::AST::VariableExpr*>(call->args[0].get())) {
                auto it = ctx.functions.find(var->name);
                if (it != ctx.functions.end() && it->second.func)
                    fnPtr = ctx.builder.CreateBitCast(it->second.func, i8p, "probe_fn");
                if (!fnPtr) {
                    LOG_ERROR(ctx.fmtLoc(call->sourceNode) +
                              "step.set_resolve: '" + var->name + "' is not a known function");
                    return nullptr;
                }
            }
            if (!fnPtr) return nullptr;
            ctx.builder.CreateCall(ctx.getFunction("aiml_step_set_resolve",
                ctx.builder.getVoidTy(), {i8p, i8p}), {self, fnPtr});
            return nullptr;
        };
        // spec/62 §20.4
        stepInfo.builtinMethods["name"]        = mkAttrStr("aiml_step_attr", "name");
        stepInfo.builtinMethods["cache"]       = mkAttrStr("aiml_step_attr", "cache");
        stepInfo.builtinMethods["per"]         = mkAttrStr("aiml_step_attr", "per");
        stepInfo.builtinMethods["needs_count"] = mkGetI32("aiml_step_needs_count");
        stepInfo.builtinMethods["need_at"]     = mkIdxStr("aiml_step_need_at");
        ctx.classes["step"] = std::move(stepInfo);

        CompilerContext::ClassInfo stageInfo;
        stageInfo.name = "stage"; stageInfo.isBuiltin = true;
        stageInfo.builtinMethods["set_name"]    = mkStr("aiml_stage_set_name");
        stageInfo.builtinMethods["set_runs_on"] = mkStr("aiml_stage_set_runs_on");
        stageInfo.builtinMethods["add_step"]    = mkHandle("aiml_stage_add_step");
        // The distributed keys (spec/62 §18): written by the
        // rewrite, read by the host (§20.4).
        stageInfo.builtinMethods["set_attr"]  = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            if (call->args.size() < 2) return nullptr;
            llvm::Value* self = ctx.builder.CreateLoad(i8p, base, "self");
            llvm::Value* key  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* val  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            ctx.builder.CreateCall(ctx.getFunction("aiml_stage_set_attr",
                ctx.builder.getVoidTy(), {i8p, i8p, i8p}), {self, key, val});
            return nullptr;
        };
        stageInfo.builtinMethods["set_limit"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            if (call->args.size() < 2) return nullptr;
            llvm::Value* self = ctx.builder.CreateLoad(i8p, base, "self");
            llvm::Value* key  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* val  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            if (val->getType() != i32) val = ctx.builder.CreateZExtOrTrunc(val, i32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_stage_set_limit",
                ctx.builder.getVoidTy(), {i8p, i8p, i32}), {self, key, val});
            return nullptr;
        };
        stageInfo.builtinMethods["name"]       = mkAttrStr("aiml_stage_attr", "name");
        stageInfo.builtinMethods["agent"]      = mkAttrStr("aiml_stage_attr", "agent");
        stageInfo.builtinMethods["steplayer"]  = mkAttrStr("aiml_stage_attr", "steplayer");
        stageInfo.builtinMethods["image"]      = mkAttrStr("aiml_stage_attr", "image");
        stageInfo.builtinMethods["base"]       = mkAttrStr("aiml_stage_attr", "base");
        stageInfo.builtinMethods["requires"]   = mkAttrStr("aiml_stage_attr", "requires");
        stageInfo.builtinMethods["shares"]     = mkAttrStr("aiml_stage_attr", "shares");
        stageInfo.builtinMethods["command"]    = mkAttrStr("aiml_stage_attr", "command");
        stageInfo.builtinMethods["cpu_cores"]  = mkAttrI32("aiml_stage_limit", "cpu");
        stageInfo.builtinMethods["mem_mb"]     = mkAttrI32("aiml_stage_limit", "mem");
        stageInfo.builtinMethods["pids_max"]   = mkAttrI32("aiml_stage_limit", "pids");
        stageInfo.builtinMethods["timeout_ms"] = mkAttrI32("aiml_stage_limit", "timeout");
        stageInfo.builtinMethods["step_count"] = mkGetI32("aiml_stage_step_count");
        stageInfo.builtinMethods["step_at"]    = mkIdxHandle("aiml_stage_step_at", "stage.step_at");
        ctx.classes["stage"] = std::move(stageInfo);

        CompilerContext::ClassInfo pipeInfo;
        pipeInfo.name = "pipeline"; pipeInfo.isBuiltin = true;
        pipeInfo.builtinMethods["set_name"]  = mkStr("aiml_pipeline_set_name");
        pipeInfo.builtinMethods["add_stage"]   = mkHandle("aiml_pipeline_add_stage");
        pipeInfo.builtinMethods["axis_type"] = mkIdxStr("aiml_pipeline_axis_type");
        pipeInfo.builtinMethods["add_exclude"] = mkStr("aiml_pipeline_add_exclude");
        pipeInfo.builtinMethods["lane_count"]   = mkGetI32("aiml_pipeline_lane_count");
        pipeInfo.builtinMethods["add_push"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            if (call->args.size() < 3) return nullptr;
            llvm::Value* self = ctx.builder.CreateLoad(i8p, base, "self");
            llvm::Value* from = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* to   = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            llvm::Value* tag  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[2].get())));
            ctx.builder.CreateCall(ctx.getFunction("aiml_pipeline_add_push",
                ctx.builder.getVoidTy(), {i8p, i8p, i8p, i8p}), {self, from, to, tag});
            return nullptr;
        };
        // spec/62 §20.4
        pipeInfo.builtinMethods["name"]             = mkGetStr("aiml_pipeline_name_of");
        pipeInfo.builtinMethods["stage_count"]      = mkGetI32("aiml_pipeline_stage_count");
        pipeInfo.builtinMethods["stage_at"]         = mkIdxHandle("aiml_pipeline_stage_at", "pipeline.stage_at");
        pipeInfo.builtinMethods["lane_bindings"]    = mkIdxStr("aiml_pipeline_lane_bindings");
        pipeInfo.builtinMethods["axis_count"]       = mkGetI32("aiml_pipeline_axis_count");
        pipeInfo.builtinMethods["axis_name"]        = mkIdxStr("aiml_pipeline_axis_name");
        pipeInfo.builtinMethods["axis_value_count"] = mkIdxI32("aiml_pipeline_axis_value_count");
        pipeInfo.builtinMethods["axis_value"]       = mkIdx2Str("aiml_pipeline_axis_value");
        pipeInfo.builtinMethods["push_count"]       = mkGetI32("aiml_pipeline_push_count");
        pipeInfo.builtinMethods["push_from"]        = mkIdxKeyStr("aiml_pipeline_push_field", "from");
        pipeInfo.builtinMethods["push_to"]          = mkIdxKeyStr("aiml_pipeline_push_field", "to");
        pipeInfo.builtinMethods["push_tag"]         = mkIdxKeyStr("aiml_pipeline_push_field", "tag");
        // run() or run(observer) — spec/62 §13.2.  The observer is a
        // function (StepRun) -> void; without it, it runs just the same (F62-79).
        pipeInfo.builtinMethods["run"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* self = ctx.builder.CreateLoad(i8p, base, "self");
            if (call->args.empty()) {
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction("aiml_pipeline_run", i32, {i8p}), {self}));
            }
            // The argument is a FUNCTION NAME; we need its address.
            llvm::Value* fnPtr = nullptr;
            if (auto* var = dynamic_cast<AIML::AST::VariableExpr*>(call->args[0].get())) {
                if (auto* fi = ctx.functions.count(var->name) ? &ctx.functions[var->name] : nullptr) {
                    if (fi->func) fnPtr = ctx.builder.CreateBitCast(fi->func, i8p, "obs_fn");
                }
                if (!fnPtr) {
                    LOG_ERROR(ctx.fmtLoc(call->sourceNode) +
                              "pipeline.run: '" + var->name + "' is not a known function");
                    return ctx.ir_gen->getInt32(-1);
                }
            }
            if (!fnPtr) fnPtr = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_pipeline_run_observed", i32, {i8p, i8p}), {self, fnPtr}));
        };
        pipeInfo.builtinMethods["reused_count"] = mkGetI32("aiml_pipeline_reused_count");
        pipeInfo.builtinMethods["ran_count"]    = mkGetI32("aiml_pipeline_ran_count");
        pipeInfo.builtinMethods["attempt_count"] = mkGetI32("aiml_pipeline_attempt_count");
        pipeInfo.builtinMethods["status"]        = mkGetI32("aiml_pipeline_status");
        // spec/62 §5.9
        pipeInfo.builtinMethods["lifecycle"]     = mkGetI32("aiml_pipeline_lifecycle");
        pipeInfo.builtinMethods["set_parallel"]  = mkI32("aiml_pipeline_set_parallel");
        pipeInfo.builtinMethods["set_workspace_root"] = mkStr("aiml_pipeline_set_workspace_root");
        pipeInfo.builtinMethods["release"]       = mkStr("aiml_pipeline_release");
        pipeInfo.builtinMethods["request_pause"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            (void)call; (void)evalExpr;
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* self = ctx.builder.CreateLoad(i8p, base, "self");
            ctx.builder.CreateCall(ctx.getFunction("aiml_pipeline_request_pause",
                llvm::Type::getVoidTy(ctx.context), {i8p}), {self});
            return nullptr;
        };
        pipeInfo.builtinMethods["resume"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            (void)call; (void)evalExpr;
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* self = ctx.builder.CreateLoad(i8p, base, "self");
            ctx.builder.CreateCall(ctx.getFunction("aiml_pipeline_resume",
                llvm::Type::getVoidTy(ctx.context), {i8p}), {self});
            return nullptr;
        };
        // add_param(axis, list<string>) or add_param(axis, type, list<string>).
        //
        // spec/62 §4.1: an axis HAS a type (F62-1) — the declared form
        // produces the three-argument version.  Composed (§13.5) it stays
        // two-argument: there the body reads `step.arg` itself, so the type is
        // `string`.  An extra argument, not a second format.
        pipeInfo.builtinMethods["add_param"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p   = ctx.builder.getInt8Ty()->getPointerTo();
            auto* ptrTy = llvm::PointerType::get(ctx.context, 0);
            if (call->args.size() < 2) return nullptr;
            const bool typed = call->args.size() >= 3;
            llvm::Value* self = ctx.builder.CreateLoad(i8p, base, "self");
            llvm::Value* axis = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* type = typed
                ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())))
                : ctx.builder.CreateGlobalStringPtr("string", "axis_type");
            llvm::Value* lst  = static_cast<llvm::Value*>(
                ctx.ir_gen->unwrap(evalExpr(call->args[typed ? 2 : 1].get())));
            if (!lst->getType()->isPointerTy()) {
                llvm::AllocaInst* spill = ctx.createEntryAlloca(lst->getType(), "param_spill");
                ctx.builder.CreateStore(lst, spill);
                lst = spill;
            }
            ctx.builder.CreateCall(ctx.getFunction("aiml_pipeline_add_param",
                ctx.builder.getVoidTy(), {i8p, i8p, i8p, ptrTy}), {self, axis, type, lst});
            return nullptr;
        };
        // spec/62 §13.1: StepRun — what the observer gets.
        // The layout MUST match AimlStepRun in pipeline_runtime.h.
        {
            auto* pTy  = llvm::PointerType::get(ctx.context, 0);
            auto* i32t = ctx.builder.getInt32Ty();
            llvm::StructType* srTy = llvm::StructType::getTypeByName(ctx.context, "StepRun");
            if (!srTy) srTy = llvm::StructType::create(ctx.context,
                {pTy, pTy, pTy, i32t, i32t, i32t}, "StepRun");
            CompilerContext::ClassInfo sr;
            sr.name = "StepRun"; sr.type = srTy; sr.isBuiltin = false;
            auto mkm = [](llvm::Type* t, int idx, const std::string& tn) {
                CompilerContext::StructMember m; m.type = t; m.index = idx; m.typeName = tn; return m;
            };
            sr.members["name"]      = mkm(pTy,  0, "string");
            sr.members["bindings"]  = mkm(pTy,  1, "string");
            sr.members["step_key"]  = mkm(pTy,  2, "string");
            sr.members["lane"]      = mkm(i32t, 3, "i32");
            sr.members["reused"]    = mkm(i32t, 4, "i32");
            sr.members["exit_code"] = mkm(i32t, 5, "i32");
            ctx.classes["StepRun"] = std::move(sr);
        }

        ctx.classes["pipeline"] = std::move(pipeInfo);

        // Konstruktoren als Namespace-Funktionen: step.new() / stage.new() / pipeline.new()
        auto mkNew = [](const char* cfn) {
            return [cfn](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
                auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(ctx.getFunction(cfn, i8p, {}), {}));
            };
        };
        ctx.namespaces["step"].builtinFuncs["new"]     = mkNew("aiml_step_new");
        ctx.namespaces["stage"].builtinFuncs["new"]    = mkNew("aiml_stage_new");
        ctx.namespaces["pipeline"].builtinFuncs["new"] = mkNew("aiml_pipeline_new");

        // spec/62 §5.7 / F62-90: what the body says about its own outcome.
        // In the namespace `step`, because a step body is an
        // ordinary function here and is also compiled in the ordinary context
        // — a name that only existed in the body would be
        // 'Unknown function' there.  The EFFECT depends on the running step.
        ctx.namespaces["step"].builtinFuncs["set_status"] =
            [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                auto* i32t = ctx.builder.getInt32Ty();
                if (args.empty()) return nullptr;
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                ctx.builder.CreateCall(ctx.getFunction("aiml_step_status_set",
                    llvm::Type::getVoidTy(ctx.context), {i32t}), {v});
                return nullptr;
            };
        auto mkFail = [](const char* cfn) {
            return [cfn](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                if (args.empty()) return nullptr;
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                ctx.builder.CreateCall(ctx.getFunction(cfn,
                    llvm::Type::getVoidTy(ctx.context), {i8p}), {v});
                return nullptr;
            };
        };
        // spec/62 §13.5e: how a body sees its lane.
        auto mkStrRet = [](const char* cfn, bool takesName) {
            return [cfn, takesName](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                std::vector<llvm::Type*> params;
                std::vector<llvm::Value*> callArgs;
                if (takesName) {
                    if (args.empty()) return nullptr;
                    params.push_back(i8p);
                    callArgs.push_back(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])));
                }
                llvm::Value* res = ctx.builder.CreateCall(ctx.getFunction(cfn, i8p, params), callArgs);
                AIML::AST::aimlTrackStringTemp(ctx, res);
                return ctx.ir_gen->wrap(res);
            };
        };
        ctx.namespaces["step"].builtinFuncs["arg"]       = mkStrRet("aiml_step_arg", true);
        ctx.namespaces["step"].builtinFuncs["host"]      = mkStrRet("aiml_step_host", true);
        ctx.namespaces["step"].builtinFuncs["workspace"] = mkStrRet("aiml_step_workspace", false);
        ctx.namespaces["step"].builtinFuncs["need_result"] = mkStrRet("aiml_step_need_result", true);
        // spec/62 F62-118: a number is published in decimal — what is recorded
        // is the serialised form, and that is text.
        ctx.namespaces["step"].builtinFuncs["set_result"] =
            [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                if (args.empty()) return nullptr;
                llvm::Value* raw = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                if (raw && raw->getType()->isIntegerTy()) {
                    llvm::Value* narrow = ctx.builder.CreateSExtOrTrunc(raw, ctx.builder.getInt32Ty());
                    raw = StringUtils::intToString(ctx, narrow);
                }
                ctx.builder.CreateCall(ctx.getFunction("aiml_step_set_result",
                    llvm::Type::getVoidTy(ctx.context), {i8p}), {raw});
                return nullptr;
            };
        ctx.namespaces["step"].builtinFuncs["fail_lane"]     = mkFail("aiml_step_fail_lane");
        ctx.namespaces["step"].builtinFuncs["fail_pipeline"] = mkFail("aiml_step_fail_pipeline");
        (void)i8p; (void)i32; (void)vd;
    }


    // ── path builtin class (instance methods) ────────────────────────────────
    // path values are i8* pointers into the global intern table.
    // The base alloca holds an i8* — we load it before every call.
    {
        CompilerContext::ClassInfo pi;
        pi.name      = "path";
        pi.isBuiltin = true;

        llvm::Type* i8p_ty = llvm::Type::getInt8Ty(ctx.context)->getPointerTo();
        llvm::Type* i32_ty = llvm::Type::getInt32Ty(ctx.context);
        llvm::Type* i8_ty  = llvm::Type::getInt8Ty(ctx.context);
        llvm::Type* i64_ty = llvm::Type::getInt64Ty(ctx.context);

        // Helper: load i8* from the alloca
        auto loadPath = [](CompilerContext& ctx, llvm::Value* alloca) -> llvm::Value* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            return ctx.builder.CreateLoad(i8p, alloca, "pv");
        };
        // Helper: emit call to path runtime fn(i8*) -> i8*
        auto callP2P = [loadPath](CompilerContext& ctx, llvm::Value* alloca,
                                  const char* fn_name) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction(fn_name, i8p, {i8p});
            llvm::Value* pv   = loadPath(ctx, alloca);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {pv}));
        };
        // Same, for runtime fns returning a fresh owned rc string (statement temp, spec/61 §7)
        auto callP2S = [loadPath](CompilerContext& ctx, llvm::Value* alloca,
                                  const char* fn_name) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction(fn_name, i8p, {i8p});
            llvm::Value* fresh = ctx.builder.CreateCall(f, {loadPath(ctx, alloca)});
            if (ctx.lifetimeDropsEnabled && !ctx.stmtTempStack.empty())
                AIML::AST::aimlTrackStringTemp(ctx, fresh);
            return ctx.ir_gen->wrap(fresh);
        };
        // Helper: emit call to path runtime fn(i8*) -> i32
        auto callP2I = [loadPath](CompilerContext& ctx, llvm::Value* alloca,
                                  const char* fn_name) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction(fn_name, i32, {i8p});
            llvm::Value* pv   = loadPath(ctx, alloca);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {pv}));
        };

        // join(seg...) — sequential joins for up to N args
        pi.builtinMethods["join"] = [loadPath](CompilerContext& ctx, llvm::Value* alloca,
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> eval) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* joinFn = ctx.getFunction("aiml_path_join", i8p, {i8p, i8p});
            llvm::Value* acc = loadPath(ctx, alloca);
            for (auto& arg : call->args) {
                IRValue* av = eval(arg.get());
                if (!av) continue;
                llvm::Value* seg = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(av));
                acc = ctx.builder.CreateCall(joinFn, {acc, seg}, "joined");
            }
            return ctx.ir_gen->wrap(acc);
        };

        // safe_join(rhs)
        pi.builtinMethods["safe_join"] = [loadPath](CompilerContext& ctx, llvm::Value* alloca,
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> eval) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_path_safe_join", i8p, {i8p, i8p});
            llvm::Value* base = loadPath(ctx, alloca);
            if (call->args.empty()) return ctx.ir_gen->wrap(base);
            IRValue* rv = eval(call->args[0].get());
            llvm::Value* rhs = rv ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(rv))
                                  : ctx.builder.CreateGlobalStringPtr("");
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {base, rhs}));
        };

        pi.builtinMethods["parent"] = [callP2P](CompilerContext& ctx, llvm::Value* alloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            return callP2P(ctx, alloca, "aiml_path_parent");
        };
        pi.builtinMethods["name"] = [callP2S](CompilerContext& ctx, llvm::Value* alloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            return callP2S(ctx, alloca, "aiml_path_name");
        };
        pi.builtinMethods["stem"] = [callP2S](CompilerContext& ctx, llvm::Value* alloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            return callP2S(ctx, alloca, "aiml_path_stem");
        };
        pi.builtinMethods["extension"] = [callP2S](CompilerContext& ctx, llvm::Value* alloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            return callP2S(ctx, alloca, "aiml_path_extension");
        };
        pi.builtinMethods["root"] = [callP2P](CompilerContext& ctx, llvm::Value* alloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            return callP2P(ctx, alloca, "aiml_path_root");
        };
        pi.builtinMethods["to_string"] = [callP2S](CompilerContext& ctx, llvm::Value* alloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            return callP2S(ctx, alloca, "aiml_path_to_string");
        };
        pi.builtinMethods["normalize"] = [callP2P](CompilerContext& ctx, llvm::Value* alloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            // Already normalized at intern time; intern again for safety
            return callP2P(ctx, alloca, "aiml_path_intern");
        };

        // relative_to(base: path)
        pi.builtinMethods["relative_to"] = [loadPath](CompilerContext& ctx, llvm::Value* alloca,
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> eval) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_path_relative_to", i8p, {i8p, i8p});
            llvm::Value* pv = loadPath(ctx, alloca);
            if (call->args.empty()) return ctx.ir_gen->wrap(pv);
            IRValue* bv = eval(call->args[0].get());
            llvm::Value* base = bv ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(bv))
                                   : ctx.builder.CreateGlobalStringPtr("");
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {pv, base}));
        };

        pi.builtinMethods["is_absolute"] = [callP2I](CompilerContext& ctx, llvm::Value* alloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            return callP2I(ctx, alloca, "aiml_path_is_absolute");
        };
        pi.builtinMethods["is_relative"] = [callP2I](CompilerContext& ctx, llvm::Value* alloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            return callP2I(ctx, alloca, "aiml_path_is_relative");
        };
        pi.builtinMethods["is_empty"] = [callP2I](CompilerContext& ctx, llvm::Value* alloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            return callP2I(ctx, alloca, "aiml_path_is_empty");
        };
        pi.builtinMethods["depth"] = [callP2I](CompilerContext& ctx, llvm::Value* alloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            return callP2I(ctx, alloca, "aiml_path_depth");
        };

        // Filesystem predicates on the path itself
        pi.builtinMethods["exists"] = [callP2I](CompilerContext& ctx, llvm::Value* alloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            return callP2I(ctx, alloca, "aiml_io_exists");
        };
        pi.builtinMethods["is_dir"] = [callP2I](CompilerContext& ctx, llvm::Value* alloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            return callP2I(ctx, alloca, "aiml_io_is_dir");
        };
        pi.builtinMethods["is_file"] = [callP2I](CompilerContext& ctx, llvm::Value* alloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            return callP2I(ctx, alloca, "aiml_io_is_file");
        };

        ctx.classes["path"] = pi;
    }

    // ── path namespace (static / constructor functions) ───────────────────────
    {
        auto& pathNs = ctx.namespaces["path"];

        auto internLit = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_path_intern", i8p, {i8p});
            if (args.empty()) return ctx.ir_gen->wrap(ctx.builder.CreateGlobalStringPtr(""));
            llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {v}));
        };

        pathNs.builtinFuncs["from_string"] = internLit;

        auto noArgPathFn = [](const char* fn_name) {
            return [fn_name](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* f = ctx.getFunction(fn_name, i8p, {});
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {}));
            };
        };

        pathNs.builtinFuncs["home"]        = noArgPathFn("aiml_path_home");
        pathNs.builtinFuncs["temp_dir"]    = noArgPathFn("aiml_path_temp_dir");
        pathNs.builtinFuncs["cwd"]         = noArgPathFn("aiml_path_cwd");
        pathNs.builtinFuncs["current_exe"] = noArgPathFn("aiml_path_current_exe");

        pathNs.builtinFuncs["config_dir"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_path_config_dir", i8p, {i8p});
            llvm::Value* app = args.empty()
                ? ctx.builder.CreateGlobalStringPtr("")
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {app}));
        };
    }

    // ── yaml namespace: parsed document navigation ──────────────────────────
    {
        auto& yaml = ctx.namespaces["yaml"];

        // spec/60: yaml.tokens(text) → list<i32>, triples (start, len, kind).
        // Error-tolerant lexer for highlighting — not a parser (cosmos
        // spec/98 F98-4/F98-5).  List return like cascade.field_indices:
        // under memV3 a heap handle, otherwise an entry alloca.
        yaml.builtinFuncs["tokens"] = [](CompilerContext& ctx,
                                         const std::vector<IRValue*>& args) -> IRValue* {
            auto* i32   = ctx.builder.getInt32Ty();
            auto* i8p   = ctx.builder.getInt8Ty()->getPointerTo();
            auto* ptrTy = llvm::PointerType::get(ctx.context, 0);
            llvm::StructType* listTy = ListImpl::getListStructType(ctx);
            llvm::Value* outAlloca;
            if (ctx.memV3) {
                outAlloca = ctx.builder.CreateCall(
                    ctx.getFunction("aiml_list_handle_new", i8p, {}), {}, "yaml_tok_h");
                ListImpl::initList(ctx, outAlloca, i32, 4);
            } else {
                outAlloca = ctx.createEntryAlloca(listTy, "yaml_tok_out");
                ListImpl::initList(ctx, outAlloca, i32, 4);
            }
            if (args.empty()) return ctx.ir_gen->wrap(outAlloca);
            llvm::Value* txt = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            ctx.builder.CreateCall(
                ctx.getFunction("aiml_yaml_tokens", i32, {i8p, ptrTy}), {txt, outAlloca});
            return ctx.ir_gen->wrap(outAlloca);
        };

        auto unaryPtr = [](const char* name) {
            return [name](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                if (args.empty()) return ctx.ir_gen->wrap(llvm::ConstantPointerNull::get(i8p));
                auto* function = ctx.getFunction(name, i8p, {i8p});
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(function, {
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))}));
            };
        };
        auto unaryI32 = [](const char* name) {
            return [name](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                auto* i32 = ctx.builder.getInt32Ty();
                if (args.empty()) return ctx.ir_gen->getInt32(0);
                auto* function = ctx.getFunction(name, i32, {i8p});
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(function, {
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))}));
            };
        };
        // spec/61 §7: a unary yaml accessor that returns a FRESH owned aiml string (scalar/dump/error,
        // all via copyString → aiml_str_ndup, rc=1, no other owner). Register the result as an owned
        // stmt-temp so a binding local MOVES it (single rc ref, decref'd by its scope-drop) and an
        // unbound transient is decref'd at statement end. Without this the decl-init normalisation
        // treats it as a BORROW and increfs it (rc 1→2, slot drops to 1) → the original ref leaks
        // (the grai yaml-grep string leak).
        auto unaryStrOwned = [](const char* name) {
            return [name](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                if (args.empty()) return ctx.ir_gen->wrap(llvm::ConstantPointerNull::get(i8p));
                auto* function = ctx.getFunction(name, i8p, {i8p});
                llvm::Value* res = ctx.builder.CreateCall(function, {
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))});
                if (ctx.lifetimeDropsEnabled && !ctx.stmtTempStack.empty())
                    AIML::AST::aimlTrackStringTemp(ctx, res);
                return ctx.ir_gen->wrap(res);
            };
        };
        yaml.builtinFuncs["parse"] = unaryPtr("aiml_yaml_parse");
        yaml.builtinFuncs["parse_file"] = unaryPtr("aiml_yaml_parse_file");
        yaml.builtinFuncs["error"] = unaryStrOwned("aiml_yaml_error");
        yaml.builtinFuncs["root"] = unaryPtr("aiml_yaml_root");
        yaml.builtinFuncs["kind"] = unaryI32("aiml_yaml_kind");
        yaml.builtinFuncs["size"] = unaryI32("aiml_yaml_size");
        yaml.builtinFuncs["line"] = unaryI32("aiml_yaml_line");
        yaml.builtinFuncs["column"] = unaryI32("aiml_yaml_column");
        yaml.builtinFuncs["scalar"] = unaryStrOwned("aiml_yaml_scalar");
        yaml.builtinFuncs["dump"] = unaryStrOwned("aiml_yaml_dump");
        yaml.builtinFuncs["free"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            if (args.empty()) return nullptr;
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* function = ctx.getFunction("aiml_yaml_free", ctx.builder.getVoidTy(), {i8p});
            ctx.builder.CreateCall(function, {static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))});
            return nullptr;
        };
        auto indexedPtr = [](const char* name) {
            return [name](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                auto* i32 = ctx.builder.getInt32Ty();
                if (args.size() < 2) return ctx.ir_gen->wrap(llvm::ConstantPointerNull::get(i8p));
                auto* function = ctx.getFunction(name, i8p, {i8p, i32});
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(function, {
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))}));
            };
        };
        yaml.builtinFuncs["item"] = indexedPtr("aiml_yaml_item");
        yaml.builtinFuncs["value"] = indexedPtr("aiml_yaml_value");
        yaml.builtinFuncs["key"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            if (args.size() < 2) return ctx.ir_gen->wrap(ctx.builder.CreateGlobalStringPtr(""));
            auto* function = ctx.getFunction("aiml_yaml_key", i8p, {i8p, i32});
            // yaml.key returns a FRESH owned aiml string (copyString) → own it as a stmt-temp (see
            // unaryStrOwned) so the binding local moves it / an unbound transient is decref'd.
            llvm::Value* res = ctx.builder.CreateCall(function, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))});
            if (ctx.lifetimeDropsEnabled && !ctx.stmtTempStack.empty())
                AIML::AST::aimlTrackStringTemp(ctx, res);
            return ctx.ir_gen->wrap(res);
        };
        yaml.builtinFuncs["get"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            if (args.size() < 2) return ctx.ir_gen->wrap(llvm::ConstantPointerNull::get(i8p));
            auto* function = ctx.getFunction("aiml_yaml_get", i8p, {i8p, i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(function, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))}));
        };
    }

    // ── io namespace: path-aware filesystem operations ────────────────────────
    {
        auto& io = ctx.namespaces["io"];

        auto ioP2I = [](const char* fn_name) {
            return [fn_name](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Type* i32 = ctx.builder.getInt32Ty();
                llvm::Function* f = ctx.getFunction(fn_name, i32, {i8p});
                if (args.empty()) return ctx.ir_gen->getInt32(0);
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {v}));
            };
        };

        io.builtinFuncs["exists"]   = ioP2I("aiml_io_exists");
        io.builtinFuncs["is_dir"]   = ioP2I("aiml_io_is_dir");
        io.builtinFuncs["is_file"]  = ioP2I("aiml_io_is_file");
        io.builtinFuncs["mkdir"]    = ioP2I("aiml_io_mkdir");
        io.builtinFuncs["mkdir_all"]= ioP2I("aiml_io_mkdir_all");
        io.builtinFuncs["remove"]   = ioP2I("aiml_io_remove");
        io.builtinFuncs["can_read"] = ioP2I("aiml_io_can_read");
        io.builtinFuncs["can_write"]= ioP2I("aiml_io_can_write");
        io.builtinFuncs["can_exec"] = ioP2I("aiml_io_can_exec");

        io.builtinFuncs["file_size"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i64 = ctx.builder.getInt64Ty();
        llvm::Type* sz  = ctx.getSizeType();
            llvm::Function* f = ctx.getFunction("aiml_io_file_size", i64, {i8p});
            if (args.empty()) return ctx.ir_gen->wrap(ctx.builder.getInt64(-1));
            llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {v}));
        };

        io.builtinFuncs["remove_all"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_io_remove_all", i32, {i8p});
            if (args.empty()) return ctx.ir_gen->getInt32(-1);
            llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {v}));
        };

        io.builtinFuncs["rename"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_io_rename", i32, {i8p, i8p});
            if (args.size() < 2) return ctx.ir_gen->getInt32(-1);
            llvm::Value* src = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* dst = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {src, dst}));
        };

        // io.set_exec(path, on) → i32: set (on != 0) or remove the executable bit.
        io.builtinFuncs["set_exec"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_io_set_exec", i32, {i8p, i32});
            if (args.empty()) return ctx.ir_gen->getInt32(-1);
            llvm::Value* path = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* on = args.size() > 1
                ? ctx.builder.CreateZExtOrTrunc(
                      static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])), i32)
                : ctx.builder.getInt32(1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {path, on}));
        };

        // io.symlink(target, link) → i32: symbolischen Link anlegen.
        io.builtinFuncs["symlink"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_io_symlink", i32, {i8p, i8p});
            if (args.size() < 2) return ctx.ir_gen->getInt32(-1);
            llvm::Value* tgt = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* lnk = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {tgt, lnk}));
        };

        // io.is_symlink(path) → i32: is the path itself a link (does not follow it)?
        io.builtinFuncs["is_symlink"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_io_is_symlink", i32, {i8p});
            if (args.empty()) return ctx.ir_gen->getInt32(0);
            llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {v}));
        };

        // io.read_link(path) → string: link target ("" if not a link / error).
        io.builtinFuncs["read_link"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_io_read_link", i8p, {i8p});
            llvm::Value* v = args.empty()
                ? static_cast<llvm::Value*>(ctx.builder.CreateGlobalStringPtr(""))
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* owned = ctx.builder.CreateCall(f, {v}, "link_target");
            if (ctx.lifetimeDropsEnabled && !ctx.stmtTempStack.empty())
                AIML::AST::aimlTrackStringTemp(ctx, owned);
            return ctx.ir_gen->wrap(owned);
        };

        // io.link(src, dst) → i32: hard link (0 = ok, -1 = error/boundary/target exists).
        io.builtinFuncs["link"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_io_link", i32, {i8p, i8p});
            if (args.size() < 2) return ctx.ir_gen->getInt32(-1);
            llvm::Value* src = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* dst = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {src, dst}));
        };

        io.builtinFuncs["read_file"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_io_read_file", i8p, {i8p});
            llvm::Value* v = args.empty()
                ? static_cast<llvm::Value*>(ctx.builder.CreateGlobalStringPtr(""))
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* owned = ctx.builder.CreateCall(f, {v}, "file_text");   // owned rc string
            if (ctx.lifetimeDropsEnabled && !ctx.stmtTempStack.empty())
                AIML::AST::aimlTrackStringTemp(ctx, owned);
            return ctx.ir_gen->wrap(owned);
        };

        // io.read_asset(stem) → string content from data-Kind asset store
        // (spec/44_7).  Compile-time validation when arg is a string literal.
        io.builtinFuncs["read_asset"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            if (!args.empty() && ctx.currentCall && !ctx.currentCall->args.empty()) {
                if (auto* lit = dynamic_cast<AIML::AST::LiteralExpr*>(
                        ctx.currentCall->args[0].get())) {
                    if (lit->type == "string" &&
                        !AIML::AssetScanner::instance().has_stem(
                            AIML::AssetKind::Data, lit->value)) {
                        LOG_ERROR(ctx.fmtLoc() + std::string("io.read_asset: stem '") +
                                  lit->value + "' not in data store");
                    }
                }
            }
            llvm::Function* f = ctx.getFunction("aiml_asset_read_data", i8p, {i8p});
            if (args.empty()) return ctx.ir_gen->wrap(ctx.builder.CreateGlobalStringPtr(""));
            llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {v}));
        };

        // io.write_file(path, content) -> i32  (0=ok, -1=error)
        // content may be a string (written up to the terminating NUL) or a
        // buffer<u8> (written binary-safe with its exact length), following
        // the generic-entry-point rule used by hash.<algo>.
        // io.append_from(dst, src) -> i64: append file src to file dst (binary, streamed)
        io.builtinFuncs["append_from"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i64 = ctx.builder.getInt64Ty();
            if (args.size() < 2) return ctx.ir_gen->wrap(llvm::ConstantInt::get(i64, -1, true));
            llvm::Function* f = ctx.getFunction("aiml_io_append_from", i64, {i8p, i8p});
            llvm::Value* d = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* src = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {d, src}));
        };

        io.builtinFuncs["write_file"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p  = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32  = ctx.builder.getInt32Ty();
            if (args.size() < 2) return ctx.ir_gen->getInt32(-1);
            llvm::Value* p = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* c = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            if (c && c->getType() == ctx.getLLVMType("buffer")) {
                llvm::Value* dataP = ctx.builder.CreateExtractValue(c, {4}, "wf_buf_data");
                llvm::Value* lenV  = ctx.builder.CreateExtractValue(c, {0}, "wf_buf_len");
                llvm::Function* fb = ctx.getFunction("aiml_io_write_file_buf", i32, {i8p, i8p, i32});
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fb, {p, dataP, lenV}));
            }
            llvm::Function* f = ctx.getFunction("aiml_io_write_file", i32, {i8p, i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {p, c}));
        };

        // io.list_dir(path) -> list<path>
        // Allocates a GenericList on the stack, inits it, calls aiml_io_list_dir_fill, returns list ptr.
        io.builtinFuncs["list_dir"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p   = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32ty = ctx.builder.getInt32Ty();
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);

            // spec/61 §5 (memV3): the result is an owned list HANDLE, never a stack alloca (its
            // scope drop would free stack memory — "free(): invalid size"); cf. field_indices.
            llvm::Value* listAlloca;
            if (ctx.memV3) {
                listAlloca = ctx.builder.CreateCall(ctx.getFunction("aiml_list_handle_new", i8p, {}), {}, "listdir_h");
            } else {
                listAlloca = ctx.createEntryAlloca(ListImpl::getListStructType(ctx), "listdir_alloca");
            }
            ListImpl::initList(ctx, listAlloca, i8p, 8);

            llvm::Function* fillFn = ctx.getFunction("aiml_io_list_dir_fill", i32ty, {i8p, ptrTy});
            llvm::Value* pathVal = args.empty()
                ? ctx.builder.CreateGlobalStringPtr("")
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            ctx.builder.CreateCall(fillFn, {pathVal, listAlloca});

            return ctx.ir_gen->wrap(listAlloca);
        };
    }

    // ── socket_handle builtin class ──────────────────────────────────────────
    {
        CompilerContext::ClassInfo sockInfo;
        sockInfo.name      = "socket_handle";
        sockInfo.isBuiltin = true;

        // Helper: load the AimlSocket* from the i8* alloca
        // All methods: loadSock = CreateLoad(i8p, baseAlloca) → call aiml_net_*(loadSock, ...)

        sockInfo.builtinMethods["close"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* sockPtr = ctx.builder.CreateLoad(i8p, baseAlloca);
            llvm::Function* f = ctx.getFunction("aiml_net_close", ctx.builder.getInt32Ty(), {i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {sockPtr}));
        };

        sockInfo.builtinMethods["fd"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* sockPtr = ctx.builder.CreateLoad(i8p, baseAlloca);
            llvm::Function* f = ctx.getFunction("aiml_net_fd", ctx.builder.getInt32Ty(), {i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {sockPtr}));
        };

        sockInfo.builtinMethods["send"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* sockPtr = ctx.builder.CreateLoad(i8p, baseAlloca);
            llvm::Function* f = ctx.getFunction("aiml_net_send", ctx.builder.getInt32Ty(), {i8p, ptrTy});
            if (call->args.empty()) return ctx.ir_gen->getInt32(-1);
            IRValue* bufVal = evalExpr(call->args[0].get());
            llvm::Value* bufPtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(bufVal));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {sockPtr, bufPtr}));
        };

        sockInfo.builtinMethods["recv"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* sockPtr = ctx.builder.CreateLoad(i8p, baseAlloca);
            llvm::Function* f = ctx.getFunction("aiml_net_recv", ctx.builder.getInt32Ty(), {i8p, ptrTy});
            if (call->args.empty()) return ctx.ir_gen->getInt32(-1);
            IRValue* bufVal = evalExpr(call->args[0].get());
            llvm::Value* bufPtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(bufVal));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {sockPtr, bufPtr}));
        };

        sockInfo.builtinMethods["send_string"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* sockPtr = ctx.builder.CreateLoad(i8p, baseAlloca);
            llvm::Function* f = ctx.getFunction("aiml_net_send_string", ctx.builder.getInt32Ty(), {i8p, i8p});
            if (call->args.empty()) return ctx.ir_gen->getInt32(-1);
            IRValue* strVal = evalExpr(call->args[0].get());
            llvm::Value* strPtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(strVal));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {sockPtr, strPtr}));
        };

        sockInfo.builtinMethods["recv_string"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
            llvm::Type* i8p  = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32  = ctx.builder.getInt32Ty();
            llvm::Value* sockPtr = ctx.builder.CreateLoad(i8p, baseAlloca);
            llvm::Function* f = ctx.getFunction("aiml_net_recv_string", i8p, {i8p, i32});
            llvm::Value* maxBytes = call->args.empty()
                ? ctx.builder.getInt32(4096)
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {sockPtr, maxBytes}));
        };

        sockInfo.builtinMethods["set_timeout"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Value* sockPtr = ctx.builder.CreateLoad(i8p, baseAlloca);
            llvm::Function* f = ctx.getFunction("aiml_net_set_timeout", i32, {i8p, i32});
            llvm::Value* ms = call->args.empty()
                ? ctx.builder.getInt32(0)
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {sockPtr, ms}));
        };

        ctx.classes["socket_handle"] = std::move(sockInfo);
    }

    // ── net namespace ────────────────────────────────────────────────────────
    {
        auto& net = ctx.namespaces["net"];
        llvm::Type* i8p  = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32  = ctx.builder.getInt32Ty();
        llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);

        // net.socket(proto) → socket_handle (i8*)
        net.builtinFuncs["socket"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_net_socket", i8p, {i8p});
            llvm::Value* proto = args.empty()
                ? ctx.builder.CreateGlobalStringPtr("tcp")
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {proto}));
        };

        // net.connect(sock, host, port) → i32
        net.builtinFuncs["connect"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_net_connect", i32, {i8p, i8p, i32});
            if (args.size() < 3) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]))
            }));
        };

        // net.bind(sock, host, port) → i32
        net.builtinFuncs["bind"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_net_bind", i32, {i8p, i8p, i32});
            if (args.size() < 3) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]))
            }));
        };

        // net.listen(sock [,backlog]) → i32
        net.builtinFuncs["listen"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_net_listen", i32, {i8p, i32});
            if (args.empty()) return ctx.ir_gen->getInt32(-1);
            llvm::Value* backlog = args.size() >= 2
                ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
                : ctx.builder.getInt32(0);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                backlog
            }));
        };

        // net.accept(sock) → socket_handle (i8*)
        net.builtinFuncs["accept"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_net_accept", i8p, {i8p});
            if (args.empty()) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))
            }));
        };

        // net.send(sock, buf) → i32
        net.builtinFuncs["send"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p  = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32  = ctx.builder.getInt32Ty();
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
            llvm::Function* f = ctx.getFunction("aiml_net_send", i32, {i8p, ptrTy});
            if (args.size() < 2) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                bufferArgAddress(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])))
            }));
        };

        // net.recv(sock, buf) → i32
        net.builtinFuncs["recv"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p  = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32  = ctx.builder.getInt32Ty();
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
            llvm::Function* f = ctx.getFunction("aiml_net_recv", i32, {i8p, ptrTy});
            if (args.size() < 2) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                bufferArgAddress(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])))
            }));
        };

        // net.send_string(sock, str) → i32
        net.builtinFuncs["send_string"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_net_send_string", i32, {i8p, i8p});
            if (args.size() < 2) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
            }));
        };

        // net.recv_string(sock, max_bytes) → string (i8*)
        net.builtinFuncs["recv_string"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_net_recv_string", i8p, {i8p, i32});
            if (args.empty()) return ctx.ir_gen->wrap(ctx.builder.CreateGlobalStringPtr(""));
            llvm::Value* maxBytes = args.size() >= 2
                ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
                : ctx.builder.getInt32(4096);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                maxBytes
            }));
        };

        // net.close(sock) → i32
        net.builtinFuncs["close"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_net_close", i32, {i8p});
            if (args.empty()) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))
            }));
        };

        // net.fd(sock) → i32
        net.builtinFuncs["fd"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_net_fd", i32, {i8p});
            if (args.empty()) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))
            }));
        };

        // net.set_timeout(sock, ms) → i32
        net.builtinFuncs["set_timeout"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_net_set_timeout", i32, {i8p, i32});
            if (args.size() < 2) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
            }));
        };

        (void)ptrTy; // suppress unused warning
    }

    // ── tls_socket_handle builtin class ──────────────────────────────────────
    {
        CompilerContext::ClassInfo tlsInfo;
        tlsInfo.name      = "tls_socket_handle";
        tlsInfo.isBuiltin = true;

        tlsInfo.builtinMethods["close"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* p = ctx.builder.CreateLoad(i8p, baseAlloca);
            llvm::Function* f = ctx.getFunction("aiml_tls_close", ctx.builder.getInt32Ty(), {i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {p}));
        };

        tlsInfo.builtinMethods["fd"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* p = ctx.builder.CreateLoad(i8p, baseAlloca);
            llvm::Function* f = ctx.getFunction("aiml_tls_fd", ctx.builder.getInt32Ty(), {i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {p}));
        };

        tlsInfo.builtinMethods["send"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
            llvm::Type* i8p   = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* p = ctx.builder.CreateLoad(i8p, baseAlloca);
            llvm::Function* f = ctx.getFunction("aiml_tls_send", ctx.builder.getInt32Ty(), {i8p, ptrTy});
            if (call->args.empty()) return ctx.ir_gen->getInt32(-1);
            llvm::Value* buf = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {p, buf}));
        };

        tlsInfo.builtinMethods["recv"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
            llvm::Type* i8p   = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* p = ctx.builder.CreateLoad(i8p, baseAlloca);
            llvm::Function* f = ctx.getFunction("aiml_tls_recv", ctx.builder.getInt32Ty(), {i8p, ptrTy});
            if (call->args.empty()) return ctx.ir_gen->getInt32(-1);
            llvm::Value* buf = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {p, buf}));
        };

        tlsInfo.builtinMethods["send_string"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* p = ctx.builder.CreateLoad(i8p, baseAlloca);
            llvm::Function* f = ctx.getFunction("aiml_tls_send_string", ctx.builder.getInt32Ty(), {i8p, i8p});
            if (call->args.empty()) return ctx.ir_gen->getInt32(-1);
            llvm::Value* str = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {p, str}));
        };

        tlsInfo.builtinMethods["recv_string"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Value* p = ctx.builder.CreateLoad(i8p, baseAlloca);
            llvm::Function* f = ctx.getFunction("aiml_tls_recv_string", i8p, {i8p, i32});
            llvm::Value* maxBytes = call->args.empty()
                ? ctx.builder.getInt32(4096)
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {p, maxBytes}));
        };

        tlsInfo.builtinMethods["set_timeout"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Value* p = ctx.builder.CreateLoad(i8p, baseAlloca);
            llvm::Function* f = ctx.getFunction("aiml_tls_set_timeout", i32, {i8p, i32});
            llvm::Value* ms = call->args.empty()
                ? ctx.builder.getInt32(0)
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {p, ms}));
        };

        tlsInfo.builtinMethods["peer_cert_cn"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* p = ctx.builder.CreateLoad(i8p, baseAlloca);
            llvm::Function* f = ctx.getFunction("aiml_tls_peer_cert_cn", i8p, {i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {p}));
        };

        tlsInfo.builtinMethods["peer_cert_valid"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* p = ctx.builder.CreateLoad(i8p, baseAlloca);
            llvm::Function* f = ctx.getFunction("aiml_tls_peer_cert_valid", ctx.builder.getInt32Ty(), {i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {p}));
        };

        ctx.classes["tls_socket_handle"] = std::move(tlsInfo);
    }

    // ── tls namespace ─────────────────────────────────────────────────────────
    {
        auto& tls = ctx.namespaces["tls"];
        llvm::Type* i8p  = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32  = ctx.builder.getInt32Ty();
        llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);

        // tls.socket() → tls_socket_handle (i8*)
        tls.builtinFuncs["socket"] = [](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_tls_socket", i8p, {});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {}));
        };

        // tls.connect(sock, host, port) → i32
        tls.builtinFuncs["connect"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_tls_connect", i32, {i8p, i8p, i32});
            if (args.size() < 3) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]))
            }));
        };

        // tls.bind(sock, host, port) → i32
        tls.builtinFuncs["bind"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_tls_bind", i32, {i8p, i8p, i32});
            if (args.size() < 3) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]))
            }));
        };

        // tls.listen(sock [,backlog]) → i32
        tls.builtinFuncs["listen"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_tls_listen", i32, {i8p, i32});
            if (args.empty()) return ctx.ir_gen->getInt32(-1);
            llvm::Value* backlog = args.size() >= 2
                ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
                : ctx.builder.getInt32(0);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                backlog
            }));
        };

        // tls.accept(sock) → tls_socket_handle (i8*)
        tls.builtinFuncs["accept"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_tls_accept", i8p, {i8p});
            if (args.empty()) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))
            }));
        };

        // tls.send(sock, buf) → i32
        tls.builtinFuncs["send"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p   = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32   = ctx.builder.getInt32Ty();
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
            llvm::Function* f = ctx.getFunction("aiml_tls_send", i32, {i8p, ptrTy});
            if (args.size() < 2) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
            }));
        };

        // tls.recv(sock, buf) → i32
        tls.builtinFuncs["recv"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p   = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32   = ctx.builder.getInt32Ty();
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
            llvm::Function* f = ctx.getFunction("aiml_tls_recv", i32, {i8p, ptrTy});
            if (args.size() < 2) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
            }));
        };

        // tls.send_string(sock, str) → i32
        tls.builtinFuncs["send_string"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_tls_send_string", i32, {i8p, i8p});
            if (args.size() < 2) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
            }));
        };

        // tls.recv_string(sock, max_bytes) → string (i8*)
        tls.builtinFuncs["recv_string"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_tls_recv_string", i8p, {i8p, i32});
            if (args.empty()) return ctx.ir_gen->wrap(ctx.builder.CreateGlobalStringPtr(""));
            llvm::Value* maxBytes = args.size() >= 2
                ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
                : ctx.builder.getInt32(4096);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                maxBytes
            }));
        };

        // tls.close(sock) → i32
        tls.builtinFuncs["close"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_tls_close", i32, {i8p});
            if (args.empty()) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))
            }));
        };

        // tls.fd(sock) → i32
        tls.builtinFuncs["fd"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_tls_fd", i32, {i8p});
            if (args.empty()) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))
            }));
        };

        // tls.set_timeout(sock, ms) → i32
        tls.builtinFuncs["set_timeout"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_tls_set_timeout", i32, {i8p, i32});
            if (args.size() < 2) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
            }));
        };

        // tls.set_cert(sock, cert, key) → i32
        tls.builtinFuncs["set_cert"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_tls_set_cert", i32, {i8p, i8p, i8p});
            if (args.size() < 3) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]))
            }));
        };

        // tls.set_ca(sock, ca) → i32
        tls.builtinFuncs["set_ca"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_tls_set_ca", i32, {i8p, i8p});
            if (args.size() < 2) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
            }));
        };

        // tls.set_verify(sock, mode) → i32
        tls.builtinFuncs["set_verify"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_tls_set_verify", i32, {i8p, i8p});
            if (args.size() < 2) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
            }));
        };

        (void)ptrTy; // suppress unused warning
    }

    // ── ws_handle builtin class ───────────────────────────────────────────────
    {
        CompilerContext::ClassInfo wsInfo;
        wsInfo.name      = "ws_handle";
        wsInfo.isBuiltin = true;

        // Helper lambda that loads the AimlWs* from the i8* alloca
        wsInfo.builtinMethods["send"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Value* p = ctx.builder.CreateLoad(i8p, baseAlloca);
            llvm::Function* f = ctx.getFunction("aiml_ws_send", i32, {i8p, i8p});
            if (call->args.empty()) return ctx.ir_gen->getInt32(-1);
            llvm::Value* text = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {p, text}));
        };

        wsInfo.builtinMethods["recv"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* p = ctx.builder.CreateLoad(i8p, baseAlloca);
            llvm::Function* f = ctx.getFunction("aiml_ws_recv", i8p, {i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {p}));
        };

        wsInfo.builtinMethods["close"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* p = ctx.builder.CreateLoad(i8p, baseAlloca);
            llvm::Function* f = ctx.getFunction("aiml_ws_close", ctx.builder.getInt32Ty(), {i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {p}));
        };

        wsInfo.builtinMethods["ping"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* p = ctx.builder.CreateLoad(i8p, baseAlloca);
            llvm::Function* f = ctx.getFunction("aiml_ws_ping", ctx.builder.getInt32Ty(), {i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {p}));
        };

        wsInfo.builtinMethods["fd"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* p = ctx.builder.CreateLoad(i8p, baseAlloca);
            llvm::Function* f = ctx.getFunction("aiml_ws_fd", ctx.builder.getInt32Ty(), {i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {p}));
        };

        wsInfo.builtinMethods["poll"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Value* p = ctx.builder.CreateLoad(i8p, baseAlloca);
            llvm::Function* f = ctx.getFunction("aiml_ws_poll", i32, {i8p, i32});
            if (call->args.empty()) return ctx.ir_gen->getInt32(-1);
            llvm::Value* timeout = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {p, timeout}));
        };

        wsInfo.builtinMethods["set_timeout"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Value* p = ctx.builder.CreateLoad(i8p, baseAlloca);
            llvm::Function* f = ctx.getFunction("aiml_ws_set_timeout", i32, {i8p, i32});
            if (call->args.empty()) return ctx.ir_gen->getInt32(-1);
            llvm::Value* ms = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {p, ms}));
        };

        ctx.classes["ws_handle"] = std::move(wsInfo);
    }

    // ── SSH (spec/32_6): ssh_session / ssh_channel / ssh_test_server ─────────
    {
        // Helper macros: a method on the i8* loaded from the alloca.
        // `baseAlloca` must be loaded (pattern as for ws_handle above).
#define SSH_M0_I32(cls, mName, cName) \
        cls.builtinMethods[mName] = [](CompilerContext& ctx, llvm::Value* baseAlloca, \
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* { \
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo(); \
            llvm::Value* p = ctx.builder.CreateLoad(i8p, baseAlloca); \
            return ctx.ir_gen->wrap(ctx.builder.CreateCall( \
                ctx.getFunction(cName, ctx.builder.getInt32Ty(), {i8p}), {p})); \
        };
#define SSH_M0_STR(cls, mName, cName) \
        cls.builtinMethods[mName] = [](CompilerContext& ctx, llvm::Value* baseAlloca, \
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* { \
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo(); \
            llvm::Value* p = ctx.builder.CreateLoad(i8p, baseAlloca); \
            return ctx.ir_gen->wrap(ctx.builder.CreateCall( \
                ctx.getFunction(cName, i8p, {i8p}), {p})); \
        };
#define SSH_M0_VOID(cls, mName, cName) \
        cls.builtinMethods[mName] = [](CompilerContext& ctx, llvm::Value* baseAlloca, \
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* { \
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo(); \
            llvm::Value* p = ctx.builder.CreateLoad(i8p, baseAlloca); \
            ctx.builder.CreateCall(ctx.getFunction(cName, ctx.builder.getVoidTy(), {i8p}), {p}); \
            return nullptr; \
        };
#define SSH_M1STR_I32(cls, mName, cName) \
        cls.builtinMethods[mName] = [](CompilerContext& ctx, llvm::Value* baseAlloca, \
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* { \
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo(); \
            llvm::Value* p = ctx.builder.CreateLoad(i8p, baseAlloca); \
            llvm::Value* a = call->args.empty() \
                ? static_cast<llvm::Value*>(ctx.builder.CreateGlobalStringPtr("")) \
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get()))); \
            return ctx.ir_gen->wrap(ctx.builder.CreateCall( \
                ctx.getFunction(cName, ctx.builder.getInt32Ty(), {i8p, i8p}), {p, a})); \
        };
#define SSH_M1I32_STR(cls, mName, cName) \
        cls.builtinMethods[mName] = [](CompilerContext& ctx, llvm::Value* baseAlloca, \
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* { \
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo(); \
            llvm::Type* i32 = ctx.builder.getInt32Ty(); \
            llvm::Value* p = ctx.builder.CreateLoad(i8p, baseAlloca); \
            llvm::Value* n = call->args.empty() ? ctx.builder.getInt32(65536) \
                : ctx.builder.CreateZExtOrTrunc( \
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get()))), i32); \
            return ctx.ir_gen->wrap(ctx.builder.CreateCall( \
                ctx.getFunction(cName, i8p, {i8p, i32}), {p, n})); \
        };

        // ssh_session
        CompilerContext::ClassInfo se;
        se.name = "ssh_session";
        se.isBuiltin = true;
        SSH_M0_STR (se, "fingerprint",      "aiml_ssh_fingerprint")
        SSH_M0_STR (se, "last_error",       "aiml_ssh_last_error")
        SSH_M1STR_I32(se, "accept_host",    "aiml_ssh_accept_host")
        SSH_M1STR_I32(se, "auth_password",  "aiml_ssh_auth_password")
        SSH_M0_I32 (se, "auth_pubkey_auto", "aiml_ssh_auth_pubkey_auto")
        SSH_M0_VOID(se, "close",            "aiml_ssh_close")
        // exec(cmd) → ssh_channel
        se.builtinMethods["exec"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* p = ctx.builder.CreateLoad(i8p, baseAlloca);
            llvm::Value* cmd = call->args.empty()
                ? static_cast<llvm::Value*>(ctx.builder.CreateGlobalStringPtr(""))
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_ssh_exec", i8p, {i8p, i8p}), {p, cmd}));
        };
        ctx.classes["ssh_session"] = std::move(se);

        // ssh_channel
        CompilerContext::ClassInfo ch;
        ch.name = "ssh_channel";
        ch.isBuiltin = true;
        SSH_M1STR_I32(ch, "write",      "aiml_ssh_ch_write")
        SSH_M0_I32 (ch, "send_eof",     "aiml_ssh_ch_send_eof")
        SSH_M1I32_STR(ch, "read_out",   "aiml_ssh_ch_read_out")
        SSH_M1I32_STR(ch, "read_err",   "aiml_ssh_ch_read_err")
        SSH_M0_I32 (ch, "is_eof",       "aiml_ssh_ch_is_eof")
        SSH_M0_I32 (ch, "exit_code",    "aiml_ssh_ch_exit_code")
        SSH_M0_VOID(ch, "close",        "aiml_ssh_ch_close")
        // write_buffer(buf) → i32: the buffer<u8> is taken apart here
        // (ExtractValue {4} = data, {0} = length — as for hash.sha256).
        ch.builtinMethods["write_buffer"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Value* p = ctx.builder.CreateLoad(i8p, baseAlloca);
            if (call->args.empty()) return ctx.ir_gen->getInt32(0);
            llvm::Value* bufv = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            if (!bufv || bufv->getType() != ctx.getLLVMType("buffer")) {
                LOG_ERROR(ctx.fmtLoc() + "ssh_channel.write_buffer expects a buffer<u8> (spec/32_6 §5)");
                return ctx.ir_gen->getInt32(0);
            }
            llvm::Value* dataP = ctx.builder.CreateExtractValue(bufv, {4}, "ssh_buf_data");
            llvm::Value* lenV  = ctx.builder.CreateExtractValue(bufv, {0}, "ssh_buf_len");
            lenV = ctx.builder.CreateZExtOrTrunc(lenV, i32);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_ssh_ch_write_buffer", i32, {i8p, i8p, i32}), {p, dataP, lenV}));
        };
        ctx.classes["ssh_channel"] = std::move(ch);

        // ssh_test_server (Testbereich, spec/32_6 §7)
        CompilerContext::ClassInfo ts;
        ts.name = "ssh_test_server";
        ts.isBuiltin = true;
        SSH_M0_STR (ts, "fingerprint", "aiml_ssh_test_fingerprint")
        SSH_M0_VOID(ts, "stop",        "aiml_ssh_test_stop")
        // serve_async() → thread_handle (i8*, layout like http.start_async)
        ts.builtinMethods["serve_async"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* p = ctx.builder.CreateLoad(i8p, baseAlloca);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_ssh_test_serve_async", i8p, {i8p}), {p}));
        };
        ctx.classes["ssh_test_server"] = std::move(ts);

#undef SSH_M0_I32
#undef SSH_M0_STR
#undef SSH_M0_VOID
#undef SSH_M1STR_I32
#undef SSH_M1I32_STR

        // ── ctx: the context as a namespace of its own (spec/29_2 §14a) ────────
        //
        // `ctx.new` gives a live compiler instance; `cx.exec` runs
        // bundles in it, and later ones see what earlier ones defined.
        // The reason is the base cost: a fresh context costs ~96 ms,
        // regardless of how little it does (§15).
        //
        // NOT a security boundary (F29_2-…e): process, address space and the
        // dynamic symbol table are shared.
        {
            CompilerContext::ClassInfo cxi;
            cxi.name      = "ctx_handle";
            cxi.isBuiltin = true;

            // cx.exec(bundle) → i32
            cxi.builtinMethods["exec"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
                AIML::AST::CallExpr* call,
                std::function<AIML::IRValue*(AIML::AST::Expr*)> evalExpr) -> AIML::IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Type* i32 = ctx.builder.getInt32Ty();
                llvm::Value* self = ctx.builder.CreateLoad(i8p, baseAlloca, "cx");
                if (call->args.empty()) {
                    LOG_ERROR(ctx.fmtLoc() + "cx.exec: without a bundle there is nothing to execute");
                    return ctx.ir_gen->getInt32(-2);
                }
                llvm::Value* code = static_cast<llvm::Value*>(
                    ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction("aiml_ctx_exec_code", i32, {i8p, i8p}),
                    {self, code}, "ctx_rc"));
            };

            // cx.load(bundle) → i32: compile and link in, WITHOUT
            // running (F29_2-…n).  For a host that wants to check and READ foreign source text
            // — running it is the agent's job.
            cxi.builtinMethods["load"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
                AIML::AST::CallExpr* call,
                std::function<AIML::IRValue*(AIML::AST::Expr*)> evalExpr) -> AIML::IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Type* i32 = ctx.builder.getInt32Ty();
                llvm::Value* self = ctx.builder.CreateLoad(i8p, baseAlloca, "cx");
                if (call->args.empty()) {
                    LOG_ERROR(ctx.fmtLoc() + "cx.load: without a bundle there is nothing to compile");
                    return ctx.ir_gen->getInt32(-2);
                }
                llvm::Value* code = static_cast<llvm::Value*>(
                    ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction("aiml_ctx_load_code", i32, {i8p, i8p}),
                    {self, code}, "ctx_load_rc"));
            };

            // cx.symbol(name, type) → i64.  The type is NAMED and
            // checked at runtime (F29_2-…d); if it does not match, it throws —
            // and only then.  Two errors, two exceptions (F29_2-…d2).
            cxi.builtinMethods["symbol"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
                AIML::AST::CallExpr* call,
                std::function<AIML::IRValue*(AIML::AST::Expr*)> evalExpr) -> AIML::IRValue* {
                llvm::Type* i8p  = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Type* i32t = ctx.builder.getInt32Ty();
                llvm::Type* i64t = ctx.builder.getInt64Ty();
                llvm::Value* self = ctx.builder.CreateLoad(i8p, baseAlloca, "cx");
                auto strArg = [&](size_t i) -> llvm::Value* {
                    if (call->args.size() <= i) return ctx.builder.CreateGlobalStringPtr("");
                    return static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[i].get())));
                };
                llvm::Value* nameV = strArg(0);
                llvm::Value* typeV = strArg(1);
                llvm::Value* addrSlot  = ctx.builder.CreateAlloca(i64t, nullptr, "sym_addr");
                llvm::Value* msgSlot   = ctx.builder.CreateAlloca(i8p,  nullptr, "sym_msg");
                llvm::Value* st = ctx.builder.CreateCall(
                    ctx.getFunction("aiml_ctx_symbol", i32t,
                        {i8p, i8p, i8p, i64t->getPointerTo(), i8p->getPointerTo()}),
                    {self, nameV, typeV, addrSlot, msgSlot}, "sym_st");
                llvm::Value* msgV = ctx.builder.CreateLoad(i8p, msgSlot, "sym_msg_v");
                llvm::Value* missing = ctx.builder.CreateZExt(
                    ctx.builder.CreateICmpEQ(st, ctx.builder.getInt32(1)),
                    ctx.builder.getInt8Ty(), "sym_missing");
                emitGuardMsg(ctx, missing, "ctx_sym_missing_throw", "ctx_sym_known",
                             "SymbolMissingException", msgV);
                llvm::Value* wrong = ctx.builder.CreateZExt(
                    ctx.builder.CreateICmpEQ(st, ctx.builder.getInt32(2)),
                    ctx.builder.getInt8Ty(), "sym_wrong");
                emitGuardMsg(ctx, wrong, "ctx_sym_type_throw", "ctx_sym_ok",
                             "SymbolTypeException", msgV);
                return ctx.ir_gen->wrap(ctx.builder.CreateLoad(i64t, addrSlot, "sym"));
            };

            // cx.names(typ) → list<string>.  spec/29_2 §14b (F29_2-…i).
            cxi.builtinMethods["names"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
                AIML::AST::CallExpr* call,
                std::function<AIML::IRValue*(AIML::AST::Expr*)> evalExpr) -> AIML::IRValue* {
                llvm::Type* i8p   = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Type* i32t  = ctx.builder.getInt32Ty();
                llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
                llvm::Value* self = ctx.builder.CreateLoad(i8p, baseAlloca, "cx");
                llvm::Value* want = call->args.empty()
                    ? ctx.builder.CreateGlobalStringPtr("")
                    : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
                // spec/61 §5: a list HANDLE of its own, not a stack alloca —
                // the scope drop would otherwise free stack memory.
                llvm::Value* lst = ctx.memV3
                    ? static_cast<llvm::Value*>(ctx.builder.CreateCall(
                          ctx.getFunction("aiml_list_handle_new", i8p, {}), {}, "ctx_names_h"))
                    : static_cast<llvm::Value*>(ctx.createEntryAlloca(
                          ListImpl::getListStructType(ctx), "ctx_names"));
                ListImpl::initList(ctx, lst, i8p, 8);
                ctx.builder.CreateCall(
                    ctx.getFunction("aiml_ctx_names_fill", i32t, {i8p, i8p, ptrTy}),
                    {self, want, lst});
                return ctx.ir_gen->wrap(lst);
            };

            // cx.errors() → list<string>.  spec/29_2 §14b (F29_2-…l): `-3`
            // means "did not work" and is worthless to a host that has to tell a
            // human WHAT was wrong.
            cxi.builtinMethods["errors"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
                AIML::AST::CallExpr*,
                std::function<AIML::IRValue*(AIML::AST::Expr*)>) -> AIML::IRValue* {
                llvm::Type* i8p   = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Type* i32t  = ctx.builder.getInt32Ty();
                llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
                llvm::Value* self = ctx.builder.CreateLoad(i8p, baseAlloca, "cx");
                llvm::Value* lst = ctx.memV3
                    ? static_cast<llvm::Value*>(ctx.builder.CreateCall(
                          ctx.getFunction("aiml_list_handle_new", i8p, {}), {}, "ctx_errs_h"))
                    : static_cast<llvm::Value*>(ctx.createEntryAlloca(
                          ListImpl::getListStructType(ctx), "ctx_errs"));
                ListImpl::initList(ctx, lst, i8p, 8);
                ctx.builder.CreateCall(
                    ctx.getFunction("aiml_ctx_errors_fill", i32t, {i8p, ptrTy}), {self, lst});
                return ctx.ir_gen->wrap(lst);
            };

            // cx.call(name, "fn() -> T") — call what the context has compiled
            // (F29_2-…j).  The SIGNATURE must be a literal: the call type
            // comes from it, and that is fixed at compile time.
            // The NAME may be computed — only the run knows it.
            cxi.builtinMethods["call"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
                AIML::AST::CallExpr* call,
                std::function<AIML::IRValue*(AIML::AST::Expr*)> evalExpr) -> AIML::IRValue* {
                llvm::Type* i8p  = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Type* i32t = ctx.builder.getInt32Ty();
                llvm::Type* i64t = ctx.builder.getInt64Ty();
                if (call->args.size() < 2) {
                    LOG_ERROR(ctx.fmtLoc() + "cx.call: name and full signature, e.g. "
                              "cx.call(\"demo\", \"fn() -> pipeline\") (spec/29_2 F29_2-…j)");
                    return nullptr;
                }
                auto* sigLit = dynamic_cast<AIML::AST::LiteralExpr*>(call->args[1].get());
                if (!sigLit) {
                    LOG_ERROR(ctx.fmtLoc() + "cx.call: the signature must be written literally here — the call type "
                              "comes from it (spec/29_2 F29_2-…j)");
                    return nullptr;
                }
                // "fn(<argtypes>) -> <return>", whitespace does not count.
                std::string sig;
                for (char ch : sigLit->value) if (ch != ' ' && ch != '\t') sig += ch;
                size_t open_at = sig.find('('), close_at = sig.find(')');
                size_t arrow_at = sig.find("->");
                if (sig.compare(0, 3, "fn(") != 0 || close_at == std::string::npos ||
                    arrow_at == std::string::npos || arrow_at < close_at) {
                    LOG_ERROR(ctx.fmtLoc() + "cx.call: '" + sigLit->value + "' is not a signature — expected "
                              "is 'fn(<argtypes>) -> <return>' (spec/29_2 F29_2-…d3)");
                    return nullptr;
                }
                const std::string argPart = sig.substr(open_at + 1, close_at - open_at - 1);
                const std::string retName = sig.substr(arrow_at + 2);
                if (!argPart.empty()) {
                    LOG_ERROR(ctx.fmtLoc() + "cx.call: arguments are not implemented yet — today only "
                              "'fn() -> T' (spec/29_2 F29_2-…j)");
                    return nullptr;
                }
                llvm::Type* retTy = nullptr;
                if      (retName == "void")   retTy = ctx.builder.getVoidTy();
                else if (retName == "i32")    retTy = i32t;
                else if (retName == "i64")    retTy = i64t;
                else if (retName == "string") retTy = i8p;
                else {
                    auto cls = ctx.classes.find(retName);
                    if (cls != ctx.classes.end() && cls->second.isBuiltin) retTy = i8p;
                }
                if (!retTy) {
                    LOG_ERROR(ctx.fmtLoc() + "cx.call: return type '" + retName + "' cannot be "
                              "received here — void, i32, i64, string or a "
                              "built-in handle class (spec/29_2 F29_2-…j)");
                    return nullptr;
                }

                llvm::Value* self = ctx.builder.CreateLoad(i8p, baseAlloca, "cx");
                llvm::Value* nameV = static_cast<llvm::Value*>(
                    ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
                llvm::Value* sigV = ctx.builder.CreateGlobalStringPtr(sigLit->value, "ctx_sig");
                // Check first, then call — the same check as cx.symbol.
                llvm::Value* addrSlot = ctx.builder.CreateAlloca(i64t, nullptr, "call_addr");
                llvm::Value* msgSlot  = ctx.builder.CreateAlloca(i8p,  nullptr, "call_msg");
                llvm::Value* st = ctx.builder.CreateCall(
                    ctx.getFunction("aiml_ctx_symbol", i32t,
                        {i8p, i8p, i8p, i64t->getPointerTo(), i8p->getPointerTo()}),
                    {self, nameV, sigV, addrSlot, msgSlot}, "call_st");
                llvm::Value* msgV = ctx.builder.CreateLoad(i8p, msgSlot, "call_msg_v");
                llvm::Value* missing = ctx.builder.CreateZExt(
                    ctx.builder.CreateICmpEQ(st, ctx.builder.getInt32(1)),
                    ctx.builder.getInt8Ty(), "call_missing");
                emitGuardMsg(ctx, missing, "ctx_call_missing_throw", "ctx_call_known",
                             "SymbolMissingException", msgV);
                llvm::Value* wrong = ctx.builder.CreateZExt(
                    ctx.builder.CreateICmpEQ(st, ctx.builder.getInt32(2)),
                    ctx.builder.getInt8Ty(), "call_wrong");
                emitGuardMsg(ctx, wrong, "ctx_call_type_throw", "ctx_call_ok",
                             "SymbolTypeException", msgV);

                llvm::Value* addr = ctx.builder.CreateLoad(i64t, addrSlot, "call_sym");
                llvm::FunctionType* fnTy = llvm::FunctionType::get(retTy, {}, false);
                llvm::Value* fnPtr = ctx.builder.CreateIntToPtr(
                    addr, llvm::PointerType::getUnqual(ctx.context), "call_fn");
                llvm::Value* got = ctx.builder.CreateCall(fnTy, fnPtr, {}, 
                    retTy->isVoidTy() ? "" : "ctx_called");
                if (retTy->isVoidTy()) return ctx.ir_gen->getInt32(0);
                return ctx.ir_gen->wrap(got);
            };

            // cx.close(): explicit closing.  Afterwards all
            // addresses from this context are invalid — that is why it is an
            // action and not a side effect.
            // cx.use_packages(path) → i32: the package table for `@name/…` imports
            // of THIS context (spec/21 §3.1); "" = default lookup.  0 = set.
            cxi.builtinMethods["use_packages"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
                AIML::AST::CallExpr* call,
                std::function<AIML::IRValue*(AIML::AST::Expr*)> evalExpr) -> AIML::IRValue* {
                llvm::Type* i8p  = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Type* i32t = ctx.builder.getInt32Ty();
                llvm::Value* self = ctx.builder.CreateLoad(i8p, baseAlloca, "cx");
                llvm::Value* path = call->args.empty()
                    ? ctx.builder.CreateGlobalStringPtr("")
                    : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction("aiml_ctx_use_packages", i32t, {i8p, i8p}), {self, path}));
            };

            cxi.builtinMethods["close"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
                AIML::AST::CallExpr*,
                std::function<AIML::IRValue*(AIML::AST::Expr*)>) -> AIML::IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Value* self = ctx.builder.CreateLoad(i8p, baseAlloca, "cx");
                ctx.builder.CreateCall(
                    ctx.getFunction("aiml_ctx_free", ctx.builder.getVoidTy(), {i8p}), {self});
                return ctx.ir_gen->getInt32(0);
            };
            ctx.classes["ctx_handle"] = std::move(cxi);

            auto& cxns = ctx.namespaces["ctx"];
            cxns.builtinFuncs["new"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction("aiml_ctx_new", i8p, {}), {}, "ctx_new"));
            };
        }

        // ── ssh namespace ────────────────────────────────────────────────────
        auto& sshns = ctx.namespaces["ssh"];
        // ssh.connect(host, port, user, timeout_ms) → ssh_session
        sshns.builtinFuncs["connect"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            auto str = [&](size_t i) -> llvm::Value* {
                return args.size() > i ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]))
                                       : static_cast<llvm::Value*>(ctx.builder.CreateGlobalStringPtr(""));
            };
            auto num = [&](size_t i, int dflt) -> llvm::Value* {
                if (args.size() <= i) return ctx.builder.getInt32(dflt);
                return ctx.builder.CreateZExtOrTrunc(
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])), i32);
            };
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_ssh_connect", i8p, {i8p, i32, i8p, i32}),
                {str(0), num(1, 22), str(2), num(3, 10000)}));
        };
        // ssh.test_listen(port, user, password) → ssh_test_server
        sshns.builtinFuncs["test_listen"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Value* port = args.empty() ? ctx.builder.getInt32(0)
                : ctx.builder.CreateZExtOrTrunc(
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])), i32);
            auto str = [&](size_t i) -> llvm::Value* {
                return args.size() > i ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]))
                                       : static_cast<llvm::Value*>(ctx.builder.CreateGlobalStringPtr(""));
            };
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_ssh_test_listen", i8p, {i32, i8p, i8p}),
                {port, str(1), str(2)}));
        };
    }

    // ── Identity (spec/67): base64 / ldap / saml ──────────────────────────────
    // Runtime: src/sys/{base64,ldap,saml}_runtime.cpp.  Every returned string is
    // an aiml-managed string (tracked as a statement temp under lifetime drops,
    // like hash.<algo>); lists are real memV3 handles filled by the runtime;
    // saml.verify_response returns a map<string:string> handle (sys.env layout).
    {
        auto trackStr = ownedStringResult;
        auto newStrList = freshStringList;

        // ── base64 namespace ─────────────────────────────────────────────────
        auto& b64 = ctx.namespaces["base64"];
        auto regEncode = [&](const char* aimlName, int url) {
            b64.builtinFuncs[aimlName] = [url, trackStr](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Type* i32 = ctx.builder.getInt32Ty();
                if (args.empty()) return trackStr(ctx, ctx.builder.CreateCall(ctx.getFunction("aiml_str_dup", i8p, {i8p}), {ctx.builder.CreateGlobalStringPtr("")}));
                llvm::Value* a = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                if (a && a->getType() == ctx.getLLVMType("buffer")) {
                    // generic entry (style guideline §1): buffer<u8> → pointer + length
                    llvm::Value* dataP = ctx.builder.CreateExtractValue(a, {4}, "b64_buf_data");
                    llvm::Value* lenV  = ctx.builder.CreateExtractValue(a, {0}, "b64_buf_len");
                    return trackStr(ctx, ctx.builder.CreateCall(
                        ctx.getFunction("aiml_b64_encode", i8p, {i8p, i32, i32}),
                        {dataP, lenV, ctx.builder.getInt32(url)}));
                }
                return trackStr(ctx, ctx.builder.CreateCall(
                    ctx.getFunction("aiml_b64_encode_str", i8p, {i8p, i32}), {a, ctx.builder.getInt32(url)}));
            };
        };
        regEncode("encode", 0);
        regEncode("url_encode", 1);
        auto regDecodeBuf = [&](const char* aimlName, int url) {
            b64.builtinFuncs[aimlName] = [url](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Type* i32 = ctx.builder.getInt32Ty();
                llvm::Type* bt  = ctx.getLLVMType("buffer");
                llvm::Value* s = args.empty() ? static_cast<llvm::Value*>(ctx.builder.CreateGlobalStringPtr(""))
                                              : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                llvm::Value* hdr = ctx.builder.CreateCall(
                    ctx.getFunction("aiml_b64_decode_buf", i8p, {i8p, i32}), {s, ctx.builder.getInt32(url)});
                llvm::Function* mcf = ctx.getFunction("memcpy", i8p, {i8p, i8p, ctx.getSizeType()});
                llvm::Function* frf = ctx.getFunction("free", ctx.builder.getVoidTy(), {i8p});
                uint64_t szBytes = ctx.module().getDataLayout().getTypeAllocSize(bt);
                // Same ownership move as io.read_bytes: the heap header becomes the buffer
                // (memV3: an owned buffer handle; otherwise a stack buffer value).
                llvm::Value* out;
                if (ctx.memV3)
                    out = ctx.builder.CreateCall(ctx.getFunction("aiml_buffer_handle_new", i8p, {}), {}, "b64_buf_h");
                else
                    out = ctx.createEntryAlloca(bt, "b64_buf");
                ctx.builder.CreateCall(mcf, {out, hdr, ctx.getSizeConst(szBytes)});
                ctx.builder.CreateCall(frf, {hdr});
                return ctx.ir_gen->wrap(out);
            };
        };
        regDecodeBuf("decode", 0);
        regDecodeBuf("url_decode", 1);
        b64.builtinFuncs["decode_str"] = [trackStr](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Value* s = args.empty() ? static_cast<llvm::Value*>(ctx.builder.CreateGlobalStringPtr(""))
                                          : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* url = ctx.builder.getInt32(0);
            if (args.size() > 1) url = ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])), i32);
            return trackStr(ctx, ctx.builder.CreateCall(ctx.getFunction("aiml_b64_decode_str", i8p, {i8p, i32}), {s, url}));
        };

        // ── ldap_handle ──────────────────────────────────────────────────────
        CompilerContext::ClassInfo lh;
        lh.name = "ldap_handle";
        lh.isBuiltin = true;
        auto argStr = [](CompilerContext& ctx, AIML::AST::CallExpr* call, size_t i,
                         const std::function<IRValue*(AIML::AST::Expr*)>& evalExpr) -> llvm::Value* {
            if (!call || call->args.size() <= i) return ctx.builder.CreateGlobalStringPtr("");
            return static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[i].get())));
        };
        lh.builtinMethods["start_tls"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* h = ctx.builder.CreateLoad(i8p, base);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(ctx.getFunction("aiml_ldap_start_tls", ctx.builder.getInt32Ty(), {i8p}), {h}));
        };
        lh.builtinMethods["bind"] = [argStr](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* h = ctx.builder.CreateLoad(i8p, base);
            llvm::Value* dn = argStr(ctx, call, 0, evalExpr);
            llvm::Value* pw = argStr(ctx, call, 1, evalExpr);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(ctx.getFunction("aiml_ldap_bind", ctx.builder.getInt32Ty(), {i8p, i8p, i8p}), {h, dn, pw}));
        };
        auto regFill = [&](const char* mName, const char* cName) {
            lh.builtinMethods[mName] = [argStr, newStrList, cName](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
                llvm::Value* h = ctx.builder.CreateLoad(i8p, base);
                llvm::Value* a0 = argStr(ctx, call, 0, evalExpr);
                llvm::Value* a1 = argStr(ctx, call, 1, evalExpr);
                llvm::Value* lst = newStrList(ctx, "ldap_list");
                ctx.builder.CreateCall(ctx.getFunction(cName, ctx.builder.getInt32Ty(), {i8p, i8p, i8p, ptrTy}), {h, a0, a1, lst});
                return ctx.ir_gen->wrap(lst);
            };
        };
        regFill("search_dns", "aiml_ldap_search_dns_fill");
        regFill("attr_values", "aiml_ldap_attr_values_fill");
        lh.builtinMethods["error"] = [trackStr](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* h = ctx.builder.CreateLoad(i8p, base);
            return trackStr(ctx, ctx.builder.CreateCall(ctx.getFunction("aiml_ldap_error", i8p, {i8p}), {h}));
        };
        lh.builtinMethods["set_timeout"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Value* h = ctx.builder.CreateLoad(i8p, base);
            llvm::Value* ms = (call && !call->args.empty())
                ? ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get()))), i32)
                : ctx.builder.getInt32(10000);
            ctx.builder.CreateCall(ctx.getFunction("aiml_ldap_set_timeout", ctx.builder.getVoidTy(), {i8p, i32}), {h, ms});
            return nullptr;
        };
        lh.builtinMethods["close"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* h = ctx.builder.CreateLoad(i8p, base);
            ctx.builder.CreateCall(ctx.getFunction("aiml_ldap_close", ctx.builder.getVoidTy(), {i8p}), {h});
            // closed: the slot no longer owns a handle (a second close is a no-op)
            ctx.builder.CreateStore(llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p)), base);
            return nullptr;
        };
        ctx.classes["ldap_handle"] = std::move(lh);

        // ── ldap namespace ───────────────────────────────────────────────────
        auto& ldapns = ctx.namespaces["ldap"];
        ldapns.builtinFuncs["open"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* u = args.empty() ? static_cast<llvm::Value*>(ctx.builder.CreateGlobalStringPtr(""))
                                          : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(ctx.getFunction("aiml_ldap_open", i8p, {i8p}), {u}));
        };
        auto regStr1 = [&](CompilerContext::Namespace& ns, const char* aimlName, const char* cName) {
            ns.builtinFuncs[aimlName] = [cName, trackStr](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Value* a = args.empty() ? static_cast<llvm::Value*>(ctx.builder.CreateGlobalStringPtr(""))
                                              : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                return trackStr(ctx, ctx.builder.CreateCall(ctx.getFunction(cName, i8p, {i8p}), {a}));
            };
        };
        regStr1(ldapns, "escape_filter", "aiml_ldap_escape_filter");
        regStr1(ldapns, "escape_dn", "aiml_ldap_escape_dn");

        // ── saml namespace ───────────────────────────────────────────────────
        auto& samlns = ctx.namespaces["saml"];
        samlns.builtinFuncs["authn_request_url"] = [trackStr](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto s = [&](size_t i) -> llvm::Value* {
                return args.size() > i ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]))
                                       : static_cast<llvm::Value*>(ctx.builder.CreateGlobalStringPtr(""));
            };
            return trackStr(ctx, ctx.builder.CreateCall(
                ctx.getFunction("aiml_saml_authn_request_url", i8p, {i8p, i8p, i8p, i8p, i8p}),
                {s(0), s(1), s(2), s(3), s(4)}));
        };
        samlns.builtinFuncs["new_request_id"] = [trackStr](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            return trackStr(ctx, ctx.builder.CreateCall(ctx.getFunction("aiml_saml_new_request_id", i8p, {}), {}));
        };
        samlns.builtinFuncs["set_clock_skew"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Value* v = args.empty() ? ctx.builder.getInt32(120)
                : ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])), i32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_saml_set_clock_skew", ctx.builder.getVoidTy(), {i32}), {v});
            return nullptr;
        };
        // verify_response(...) → map<string:string> handle (same layout/ownership as sys.env).
        samlns.builtinFuncs["verify_response"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto s = [&](size_t i) -> llvm::Value* {
                return args.size() > i ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]))
                                       : static_cast<llvm::Value*>(ctx.builder.CreateGlobalStringPtr(""));
            };
            llvm::StructType* mapTy = llvm::StructType::get(ctx.context, {
                ctx.builder.getInt32Ty(), ctx.builder.getInt32Ty(), i8p, i8p->getPointerTo(), i8p->getPointerTo() });
            llvm::Value* raw = ctx.builder.CreateCall(
                ctx.getFunction("aiml_saml_verify_response", i8p, {i8p, i8p, i8p, i8p, i8p}),
                {s(0), s(1), s(2), s(3), s(4)});
            return ctx.ir_gen->wrap(ctx.builder.CreateBitCast(raw, mapTy->getPointerTo(), "saml_result_map"));
        };
    }

    // ── spec/23 §5.8: working directory, environment, files (src/sys/os_runtime.cpp) ──
    {
        auto& sysNs = ctx.namespaces["sys"];
        auto& ioNs  = ctx.namespaces["io"];
        // name(text...) → i32
        auto textToI32 = [](const char* sym, size_t arity) {
            return [sym, arity](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                llvm::Type* p = ctx.builder.getInt8Ty()->getPointerTo();
                std::vector<llvm::Type*> params(arity, p);
                std::vector<llvm::Value*> vals;
                for (size_t i = 0; i < arity; ++i) vals.push_back(argOrEmpty(ctx, args, i));
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(ctx.getFunction(sym, ctx.builder.getInt32Ty(), params), vals));
            };
        };
        // name(text) → string (managed)
        auto textToText = [](const char* sym) {
            return [sym](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                llvm::Type* p = ctx.builder.getInt8Ty()->getPointerTo();
                return ownedStringResult(ctx, ctx.builder.CreateCall(ctx.getFunction(sym, p, {p}), {argOrEmpty(ctx, args, 0)}));
            };
        };
        // name(i32) → i32
        auto intToI32 = [](const char* sym) {
            return [sym](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                llvm::Type* i32 = ctx.builder.getInt32Ty();
                llvm::Value* v = args.empty() ? ctx.builder.getInt32(-1) : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                if (v->getType()->isIntegerTy() && v->getType() != i32) v = ctx.builder.CreateIntCast(v, i32, true);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(ctx.getFunction(sym, i32, {i32}), {v}));
            };
        };
        sysNs.builtinFuncs["chdir"]    = textToI32("aiml_sys_chdir", 1);
        sysNs.builtinFuncs["setenv"]   = textToI32("aiml_sys_setenv", 2);
        sysNs.builtinFuncs["unsetenv"] = textToI32("aiml_sys_unsetenv", 1);
        sysNs.builtinFuncs["isatty"]   = intToI32("aiml_sys_isatty");
        ioNs.builtinFuncs["realpath"]  = textToText("aiml_io_realpath");
        ioNs.builtinFuncs["mkdtemp"]   = textToText("aiml_io_mkdtemp");
        ioNs.builtinFuncs["copy"]      = textToI32("aiml_io_copy", 2);
        ioNs.builtinFuncs["lock"]      = textToI32("aiml_io_lock", 1);
        ioNs.builtinFuncs["unlock"]    = intToI32("aiml_io_unlock");
    }

    // ── regex namespace (spec/09_2) and globbing (spec/42_1) ──────────────────
    // Runtime: src/sys/regex_runtime.cpp, src/sys/glob_runtime.cpp.  Strings come
    // back as managed references (statement temps), lists as list<string> filled
    // by the runtime.
    {
        auto& rx = ctx.namespaces["regex"];
        // text → i32: match (whole text), search (index or -1)
        for (const char* name : {"match", "search"}) {
            const std::string sym = std::string("aiml_regex_") + name;
            rx.builtinFuncs[name] = [sym](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                llvm::Type* p = ctx.builder.getInt8Ty()->getPointerTo();
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(ctx.getFunction(sym, ctx.builder.getInt32Ty(), {p, p}),
                                                               {argOrEmpty(ctx, args, 0), argOrEmpty(ctx, args, 1)}));
            };
        }
        // text → list<string>: find (match + groups), find_all, split
        for (const char* name : {"find", "find_all", "split"}) {
            const std::string sym = std::string("aiml_regex_") + name + "_fill";
            rx.builtinFuncs[name] = [sym](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                llvm::Type* p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Value* out = freshStringList(ctx, "regex_list");
                ctx.builder.CreateCall(ctx.getFunction(sym, ctx.builder.getInt32Ty(), {p, p, p}),
                                       {argOrEmpty(ctx, args, 0), argOrEmpty(ctx, args, 1), out});
                return ctx.ir_gen->wrap(out);
            };
        }
        rx.builtinFuncs["replace"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* p = ctx.builder.getInt8Ty()->getPointerTo();
            return ownedStringResult(ctx, ctx.builder.CreateCall(ctx.getFunction("aiml_regex_replace", p, {p, p, p}),
                {argOrEmpty(ctx, args, 0), argOrEmpty(ctx, args, 1), argOrEmpty(ctx, args, 2)}));
        };
        rx.builtinFuncs["error"] = [](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
            llvm::Type* p = ctx.builder.getInt8Ty()->getPointerTo();
            return ownedStringResult(ctx, ctx.builder.CreateCall(ctx.getFunction("aiml_regex_error", p, {}), {}));
        };

        auto& io = ctx.namespaces["io"];
        io.builtinFuncs["glob"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* out = freshStringList(ctx, "glob_list");
            ctx.builder.CreateCall(ctx.getFunction("aiml_glob_all_fill", ctx.builder.getInt32Ty(), {p, p}),
                                   {argOrEmpty(ctx, args, 0), out});
            return ctx.ir_gen->wrap(out);
        };
        io.builtinFuncs["glob_match"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* p = ctx.builder.getInt8Ty()->getPointerTo();
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(ctx.getFunction("aiml_glob_match", ctx.builder.getInt32Ty(), {p, p}),
                                                           {argOrEmpty(ctx, args, 0), argOrEmpty(ctx, args, 1)}));
        };
        // io.globber(pattern, max_per_step) → Globber
        io.builtinFuncs["globber"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Value* most = args.size() > 1 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])) : ctx.builder.getInt32(256);
            if (most->getType()->isIntegerTy() && most->getType() != i32) most = ctx.builder.CreateIntCast(most, i32, true);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(ctx.getFunction("aiml_glob_new", p, {p, i32}),
                                                           {argOrEmpty(ctx, args, 0), most}));
        };

        CompilerContext::ClassInfo ci;
        ci.name = "Globber";
        ci.isBuiltin = true;
        ci.preLoadBase = true;
        ci.builtinMethods["next"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
            llvm::Type* p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* out = freshStringList(ctx, "glob_batch");
            ctx.builder.CreateCall(ctx.getFunction("aiml_glob_next_fill", ctx.builder.getInt32Ty(), {p, p}), {base, out});
            return ctx.ir_gen->wrap(out);
        };
        for (const char* name : {"location", "pattern"}) {
            const std::string sym = std::string("aiml_glob_") + name;
            ci.builtinMethods[name] = [sym](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Type* p = ctx.builder.getInt8Ty()->getPointerTo();
                return ownedStringResult(ctx, ctx.builder.CreateCall(ctx.getFunction(sym, p, {p}), {base}));
            };
        }
        ci.builtinMethods["done"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
            llvm::Type* p = ctx.builder.getInt8Ty()->getPointerTo();
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(ctx.getFunction("aiml_glob_done", ctx.builder.getInt32Ty(), {p}), {base}));
        };
        ci.builtinMethods["free"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
            llvm::Type* p = ctx.builder.getInt8Ty()->getPointerTo();
            ctx.builder.CreateCall(ctx.getFunction("aiml_glob_free", ctx.builder.getVoidTy(), {p}), {base});
            return ctx.ir_gen->getInt32(0);
        };
        ctx.classes["Globber"] = std::move(ci);
    }

    // ── ws namespace ──────────────────────────────────────────────────────────
    {
        auto& ws = ctx.namespaces["ws"];
        llvm::Type* i8p  = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32  = ctx.builder.getInt32Ty();
        llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);

        // ws.connect(host, port, path) → ws_handle
        ws.builtinFuncs["connect"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_ws_connect", i8p, {i8p, i32, i8p});
            if (args.size() < 3) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]))
            }));
        };

        // ws.server(host, port) → ws_handle
        ws.builtinFuncs["server"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_ws_server", i8p, {i8p, i32});
            if (args.size() < 2) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
            }));
        };

        // ws.accept(srv) → ws_handle  (also handles wss servers)
        ws.builtinFuncs["accept"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_ws_accept", i8p, {i8p});
            if (args.empty()) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))
            }));
        };

        // ws.send(ws, text) → i32
        ws.builtinFuncs["send"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_ws_send", i32, {i8p, i8p});
            if (args.size() < 2) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
            }));
        };

        // ws.send_binary(ws, buf) → i32
        ws.builtinFuncs["send_binary"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p   = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32   = ctx.builder.getInt32Ty();
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
            llvm::Function* f = ctx.getFunction("aiml_ws_send_binary", i32, {i8p, ptrTy});
            if (args.size() < 2) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
            }));
        };

        // ws.send_kv_blob(ws, kv, key) → i32  (zero-copy: stream a KV blob straight from mmap)
        ws.builtinFuncs["send_kv_blob"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_ws_send_kv_blob", i32, {i8p, i8p, i8p});
            if (args.size() < 3) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]))
            }));
        };

        // ws.peer_ip(conn) → string — dotted peer IP of an accepted connection.
        // strdup so AIML owns the result (the runtime returns a struct-internal ptr).
        ws.builtinFuncs["peer_ip"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            if (args.empty()) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            llvm::Function* fip = ctx.getFunction("aiml_ws_peer_ip", i8p, {i8p});
            llvm::Value* raw = ctx.builder.CreateCall(fip, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])) });
            llvm::Function* fdup = ctx.getFunction("strdup", i8p, {i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fdup, {raw}));
        };

        // ws.recv(ws) → string
        ws.builtinFuncs["recv"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_ws_recv", i8p, {i8p});
            if (args.empty()) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))
            }));
        };

        // ws.ping(ws) → i32
        ws.builtinFuncs["ping"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_ws_ping", i32, {i8p});
            if (args.empty()) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))
            }));
        };

        // ws.close(ws) → i32
        ws.builtinFuncs["close"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_ws_close", i32, {i8p});
            if (args.empty()) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))
            }));
        };

        // ws.fd(ws) → i32
        ws.builtinFuncs["fd"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_ws_fd", i32, {i8p});
            if (args.empty()) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))
            }));
        };

        // ws.poll(ws, timeout_ms) → i32  (1=data ready, 0=timeout, -1=error)
        ws.builtinFuncs["poll"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_ws_poll", i32, {i8p, i32});
            if (args.size() < 2) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
            }));
        };

        // ws.poll_listener(srv, timeout_ms) → i32 — advances pending handshakes; 1 if a
        // handshaken client is ready, 0 on timeout, -1 on error (spec/32_4 §5.1).
        ws.builtinFuncs["poll_listener"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_ws_poll_listener", i32, {i8p, i32});
            if (args.size() < 2) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
            }));
        };

        // ws.set_timeout(ws, ms) → i32 — listener: handshake timeout; conn: recv/send timeout.
        ws.builtinFuncs["set_timeout"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_ws_set_timeout", i32, {i8p, i32});
            if (args.size() < 2) return ctx.ir_gen->getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
            }));
        };

        (void)i32; (void)ptrTy;
    }

    // ── wss namespace ─────────────────────────────────────────────────────────
    {
        auto& wss = ctx.namespaces["wss"];
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32 = ctx.builder.getInt32Ty();

        // wss.connect(host, port, path) → ws_handle  (TLS)
        wss.builtinFuncs["connect"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_wss_connect", i8p, {i8p, i32, i8p});
            if (args.size() < 3) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]))
            }));
        };

        // wss.server(host, port, cert, key) → ws_handle  (TLS listener)
        wss.builtinFuncs["server"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_wss_server", i8p, {i8p, i32, i8p, i8p});
            if (args.size() < 4) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[3]))
            }));
        };

        // wss.accept(srv) → ws_handle  (delegates to aiml_ws_accept — it's TLS-aware)
        wss.builtinFuncs["accept"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_ws_accept", i8p, {i8p});
            if (args.empty()) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))
            }));
        };

        (void)i32;
    }

    // ── db namespace ─────────────────────────────────────────────────────────
    // db.open("name")      → Store<T>  (reads ctx.deserializeTargetType for elem type)
    // db.open_kv("name")   → Kv<K:V>   (reads ctx.deserializeTargetType for val type)
    // db.open_vec("n", d)  → VecStore
    // db.begin/commit/rollback/checkpoint → stubs (i32 0)
    {
        // ── netfs: network drives without a mount and without root (spec/63) ─
        // Bound via dlopen (F63-1/§63.2): aiml gets no
        // build dependency on Samba, and if the library is missing there is a
        // clear message instead of a link error on every build.
        {
            auto& netfs = ctx.namespaces["netfs"];
            auto* i8p   = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32t  = ctx.builder.getInt32Ty();
            netfs.builtinFuncs["available"] =
                [](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
                    return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                        ctx.getFunction("aiml_netfs_available", ctx.builder.getInt32Ty(), {}), {}));
                };
            netfs.builtinFuncs["cred_from_file"] =
                [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                    auto* p8 = ctx.builder.getInt8Ty()->getPointerTo();
                    if (args.empty()) return nullptr;
                    llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                    return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                        ctx.getFunction("aiml_netfs_cred_from_file", ctx.builder.getInt32Ty(), {p8}), {v}));
                };
            netfs.builtinFuncs["connect"] =
                [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                    auto* p8 = ctx.builder.getInt8Ty()->getPointerTo();
                    auto* i32 = ctx.builder.getInt32Ty();
                    if (args.size() < 2) return nullptr;
                    llvm::Value* url  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                    llvm::Value* cred = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
                    return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                        ctx.getFunction("aiml_netfs_connect", i32, {p8, i32}), {url, cred}));
                };
            auto netStr0 = [](const char* cfn) {
                return [cfn](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
                    auto* p8 = ctx.builder.getInt8Ty()->getPointerTo();
                    llvm::Value* r = ctx.builder.CreateCall(ctx.getFunction(cfn, p8, {}), {});
                    AIML::AST::aimlTrackStringTemp(ctx, r);
                    return ctx.ir_gen->wrap(r);
                };
            };
            auto netStr1 = [](const char* cfn) {
                return [cfn](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                    auto* p8 = ctx.builder.getInt8Ty()->getPointerTo();
                    if (args.empty()) return nullptr;
                    llvm::Value* a = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                    llvm::Value* r = ctx.builder.CreateCall(ctx.getFunction(cfn, p8, {p8}), {a});
                    AIML::AST::aimlTrackStringTemp(ctx, r);
                    return ctx.ir_gen->wrap(r);
                };
            };
            netfs.builtinFuncs["last_error"] = netStr0("aiml_netfs_last_error");
            netfs.builtinFuncs["list"]       = netStr1("aiml_netfs_list");
            netfs.builtinFuncs["read_file"]  = netStr1("aiml_netfs_read_file");
            netfs.builtinFuncs["write_file"] =
                [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                    auto* p8 = ctx.builder.getInt8Ty()->getPointerTo();
                    if (args.size() < 2) return nullptr;
                    llvm::Value* a = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                    llvm::Value* b = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
                    return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                        ctx.getFunction("aiml_netfs_write_file", ctx.builder.getInt32Ty(), {p8, p8}), {a, b}));
                };
            netfs.builtinFuncs["close"] =
                [](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
                    ctx.builder.CreateCall(ctx.getFunction("aiml_netfs_close",
                        llvm::Type::getVoidTy(ctx.context), {}), {});
                    return nullptr;
                };
            (void)i8p; (void)i32t;
        }

        auto& db = ctx.namespaces["db"];

        // ── Insight into the OPEN stores (cosmos doc/spec/99 §99.2) ──────────
        // Only metrics, never contents (F99-7).  `open_writes` is a counter
        // and not a timestamp: two snapshots compared say "it is moving"
        // without reading the clock on the write path.
        db.builtinFuncs["open_count"] =
            [](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction("aiml_db_kv_open_count", ctx.builder.getInt32Ty(), {}), {}));
            };
        auto dbStrAt = [](const char* cfn) {
            return [cfn](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                auto* i32 = ctx.builder.getInt32Ty();
                if (args.empty()) return nullptr;
                llvm::Value* idx = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                llvm::Value* res = ctx.builder.CreateCall(ctx.getFunction(cfn, i8p, {i32}), {idx});
                AIML::AST::aimlTrackStringTemp(ctx, res);
                return ctx.ir_gen->wrap(res);
            };
        };
        auto dbI64At = [](const char* cfn) {
            return [cfn](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                auto* i32 = ctx.builder.getInt32Ty();
                auto* i64 = ctx.builder.getInt64Ty();
                if (args.empty()) return nullptr;
                llvm::Value* idx = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction(cfn, i64, {i32}), {idx}));
            };
        };
        db.builtinFuncs["open_name"]     = dbStrAt("aiml_db_kv_open_name");
        db.builtinFuncs["open_path"]     = dbStrAt("aiml_db_kv_open_path");
        db.builtinFuncs["open_wal_used"] = dbI64At("aiml_db_kv_open_wal_used");
        db.builtinFuncs["open_entries"]  = dbI64At("aiml_db_kv_open_entries");
        db.builtinFuncs["open_writes"]   = dbI64At("aiml_db_kv_open_writes");

        // db.open("name") → Store<T>
        // Options pre-parsed into ctx.dbOpenPersist / ctx.dbOpenColumnLayout by visitCall.
        db.builtinFuncs["open"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p   = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32   = ctx.builder.getInt32Ty();
            llvm::Type* i64   = ctx.builder.getInt64Ty();
        llvm::Type* sz  = ctx.getSizeType();
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);

            std::string targetType = ctx.deserializeTargetType;
            std::string elemName;
            if (targetType.rfind("Store<", 0) == 0 && targetType.back() == '>')
                elemName = targetType.substr(6, targetType.size() - 7);

            int64_t elemSize = 8;
            if (!elemName.empty() && ctx.classes.count(elemName)) {
                auto& ci = ctx.classes[elemName];
                if (ci.type) {
                    llvm::DataLayout dl(&ctx.module());
                    elemSize = static_cast<int64_t>(dl.getTypeAllocSize(ci.type));
                } else if (ci.packed_size > 0) {
                    elemSize = static_cast<int64_t>(ci.packed_size);
                }
            }

            llvm::Value* nameVal = args.empty()
                ? ctx.builder.CreateGlobalStringPtr("")
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* tnVal = ctx.builder.CreateGlobalStringPtr(
                elemName.empty() ? "?" : elemName);

            // ── Column Layout ─────────────────────────────────────────────────
            if (ctx.dbOpenColumnLayout && !elemName.empty() &&
                ctx.classes.count(elemName) && ctx.classes[elemName].type) {
                auto& ci = ctx.classes[elemName];
                llvm::DataLayout dl(&ctx.module());
                int n = (int)ci.members.size();
                std::vector<std::pair<int,int>> flds;
                for (auto& [fname, fm] : ci.members)
                    if (fm.type) flds.push_back({fm.index, (int)dl.getTypeAllocSize(fm.type)});
                std::sort(flds.begin(), flds.end());

                auto* arrTy = llvm::ArrayType::get(i32, (size_t)n);
                std::vector<llvm::Constant*> cs;
                for (auto& [idx, sz] : flds) cs.push_back(ctx.builder.getInt32(sz));
                auto* gv = new llvm::GlobalVariable(ctx.module(), arrTy, true,
                    llvm::GlobalValue::InternalLinkage,
                    llvm::ConstantArray::get(arrTy, cs), ".col_fsizes");

                llvm::Function* f = ctx.getFunction("aiml_db_store_open_col",
                    i8p, {i8p, i8p, i32, ptrTy, i64});
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(f,
                    {nameVal, tnVal, ctx.builder.getInt32(n), gv,
                     ctx.builder.getInt64(elemSize)}));
            }

            bool hasHeap = elemName != "string"
                && ctx.classes.count(elemName) && ctx.classes[elemName].hasHeapFields
                && ctx.classes[elemName].serNodesVar;

            // ── Persistent (WAL + mmap / typed WAL) ──────────────────────────
            if (ctx.dbOpenPersist) {
                // path:nullptr → C-Resolver (resolve_aiondb_dir) nutzt
                // db.set_path / AIONDB_PATH / Fallback einheitlich.
                llvm::Value* pathVal = ctx.dbOpenBasePathVal
                    ? ctx.dbOpenBasePathVal
                    : llvm::ConstantPointerNull::get(
                          llvm::PointerType::getUnqual(ctx.context));
                if (hasHeap) {
                    auto& ci = ctx.classes[elemName];
                    llvm::Value* nodesPtr = ci.serNodesVar;
                    llvm::Value* nNodes   = ctx.builder.getInt32((uint32_t)ci.serNodesCount);
                    llvm::Function* f = ctx.getFunction("aiml_db_store_open_typed_persist",
                        i8p, {i8p, i8p, i64, i8p, i8p, i32});
                    return ctx.ir_gen->wrap(ctx.builder.CreateCall(f,
                        {nameVal, tnVal, ctx.builder.getInt64(elemSize), pathVal, nodesPtr, nNodes}));
                }
                llvm::Function* f = ctx.getFunction("aiml_db_store_open_persist",
                    i8p, {i8p, i8p, i64, i8p});
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(f,
                    {nameVal, tnVal, ctx.builder.getInt64(elemSize), pathVal}));
            }

            // ── Default: in-memory ────────────────────────────────────────────
            // A heap-bearing element type opens a TYPED in-memory store so insert can retain the
            // element's strings/containers (§7 share) — else reusing the source model after insert
            // dangles the stored record's strings (test_model).
            if (hasHeap) {
                auto& ci = ctx.classes[elemName];
                llvm::Function* ft = ctx.getFunction("aiml_db_store_open_typed",
                    i8p, {i8p, i8p, i64, i8p, i32});
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(ft,
                    {nameVal, tnVal, ctx.builder.getInt64(elemSize),
                     ci.serNodesVar, ctx.builder.getInt32((uint32_t)ci.serNodesCount)}));
            }
            llvm::Function* f = ctx.getFunction("aiml_db_store_open", i8p, {i8p, i8p, i64});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f,
                {nameVal, tnVal, ctx.builder.getInt64(elemSize)}));
        };

        // db.open_kv("name") → Kv<K:V>
        db.builtinFuncs["open_kv"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i64 = ctx.builder.getInt64Ty();
        llvm::Type* sz  = ctx.getSizeType();
            llvm::Type* i32 = ctx.builder.getInt32Ty();

            // Extract value type from deserializeTargetType "Kv<string:ValueType>"
            std::string targetType = ctx.deserializeTargetType;
            std::string vName = "string";
            if (targetType.rfind("Kv<", 0) == 0 && targetType.back() == '>') {
                std::string inner = targetType.substr(3, targetType.size() - 4);
                size_t colon = inner.find(':');
                if (colon != std::string::npos) vName = inner.substr(colon + 1);
            }

            int64_t valSize = 0; // 0 = string
            if      (vName == "string") valSize = 0;
            else if (vName == "i32")    valSize = 4;
            else if (vName == "i64")    valSize = 8;
            else if (vName == "f32")    valSize = 4;
            else if (vName == "f64")    valSize = 8;
            else if (ctx.classes.count(vName) && ctx.classes[vName].type) {
                llvm::DataLayout dl(&ctx.module());
                valSize = static_cast<int64_t>(dl.getTypeAllocSize(ctx.classes[vName].type));
            }

            llvm::Value* nameVal = args.empty()
                ? ctx.builder.CreateGlobalStringPtr("")
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));

            // Spec 45: typed Kv — use WAL-safe open when value type has heap fields.
            bool hasHeap = vName != "string"
                && ctx.classes.count(vName) && ctx.classes[vName].hasHeapFields
                && ctx.classes[vName].serNodesVar;
            llvm::Value* kvHandle = nullptr;
            if (hasHeap) {
                auto& ci = ctx.classes[vName];
                llvm::Value* nodesPtr = ci.serNodesVar;
                llvm::Value* nNodes   = ctx.builder.getInt32((uint32_t)ci.serNodesCount);
                if (ctx.dbOpenPersist) {
                    llvm::Value* pathVal = ctx.dbOpenBasePathVal
                        ? ctx.dbOpenBasePathVal
                        : llvm::ConstantPointerNull::get(
                              llvm::PointerType::getUnqual(ctx.context));
                    // Separate _idx entry points instead of a changed signature:
                    // getFunction does NOT check signatures (the first declarer
                    // wins), and aiml_db_kv_open_persist is also declared in
                    // tensor_runtime.cpp.
                    if (ctx.dbOpenIndexDump) {
                        llvm::Function* f = ctx.getFunction("aiml_db_kv_open_typed_persist_idx",
                            i8p, {i8p, i64, i8p, i8p, i32, i32});
                        kvHandle = ctx.builder.CreateCall(f,
                            {nameVal, ctx.builder.getInt64(valSize), pathVal, nodesPtr, nNodes,
                             ctx.builder.getInt32(1)});
                    } else {
                        llvm::Function* f = ctx.getFunction("aiml_db_kv_open_typed_persist",
                            i8p, {i8p, i64, i8p, i8p, i32});
                        kvHandle = ctx.builder.CreateCall(f,
                            {nameVal, ctx.builder.getInt64(valSize), pathVal, nodesPtr, nNodes});
                    }
                } else {
                    llvm::Function* f = ctx.getFunction("aiml_db_kv_open_typed", i8p, {i8p, i64, i8p, i32});
                    kvHandle = ctx.builder.CreateCall(f,
                        {nameVal, ctx.builder.getInt64(valSize), nodesPtr, nNodes});
                }
            } else if (ctx.dbOpenPersist) {
                // path:nullptr → C-Resolver (resolve_aiondb_dir) nutzt
                // db.set_path / AIONDB_PATH / Fallback einheitlich.
                llvm::Value* pathVal = ctx.dbOpenBasePathVal
                    ? ctx.dbOpenBasePathVal
                    : llvm::ConstantPointerNull::get(
                          llvm::PointerType::getUnqual(ctx.context));
                if (ctx.dbOpenIndexDump) {
                    llvm::Function* f = ctx.getFunction("aiml_db_kv_open_persist_idx",
                        i8p, {i8p, i64, i8p, i32});
                    kvHandle = ctx.builder.CreateCall(f,
                        {nameVal, ctx.builder.getInt64(valSize), pathVal, ctx.builder.getInt32(1)});
                } else {
                    llvm::Function* f = ctx.getFunction("aiml_db_kv_open_persist", i8p, {i8p, i64, i8p});
                    kvHandle = ctx.builder.CreateCall(f,
                        {nameVal, ctx.builder.getInt64(valSize), pathVal});
                }
            } else {
                llvm::Function* f = ctx.getFunction("aiml_db_kv_open", i8p, {i8p, i64});
                kvHandle = ctx.builder.CreateCall(f, {nameVal, ctx.builder.getInt64(valSize)});
            }

            // Secondary indexes: for each db_indexed field on the value type,
            // emit a call to aiml_db_index_open_for_kv (registers the index
            // handle for later findings_by/finding_by lookups) and then a
            // backfill call so the index is consistent with any existing kv
            // entries (cross-process reopen, or first-time enabling the
            // modifier on an already-populated Kv).
            if (ctx.classes.count(vName) && ctx.classes[vName].type) {
                auto& vci = ctx.classes[vName];
                llvm::StructType* vStructTy = vci.type;
                llvm::DataLayout dl(&ctx.module());
                const llvm::StructLayout* sl = dl.getStructLayout(vStructTy);
                for (auto& [fname, mem] : vci.members) {
                    if (!mem.db_indexed) continue;

                    llvm::Value* fNameVal = ctx.builder.CreateGlobalStringPtr(fname);
                    llvm::Value* modeVal  = ctx.builder.CreateGlobalStringPtr(mem.db_indexed_mode);
                    llvm::Value* uniqVal  = ctx.builder.getInt32(mem.db_indexed_unique ? 1 : 0);
                    llvm::Function* idxOpen = ctx.getFunction(
                        "aiml_db_index_open_for_kv", i8p, {i8p, i8p, i32, i8p});
                    llvm::Value* idxHandle = ctx.builder.CreateCall(
                        idxOpen, {kvHandle, fNameVal, uniqVal, modeVal});

                    // Backfill tag — must match AimlIndexFieldTag in db_runtime.cpp.
                    int32_t tag = -1;
                    const std::string& fTy = mem.typeName;
                    if      (fTy == "string")             tag = 0;
                    else if (fTy == "i32")                tag = 1;
                    else if (fTy == "i64")                tag = 2;
                    // spec/61 §5: under memV3 a list<string> FIELD is a HANDLE pointer (tag 4), not the
                    // inline {size,cap,data} struct (tag 3) — the backfill must deref it.
                    else if (fTy == "list<string>")       tag = ctx.memV3 ? 4 : 3;
                    if (tag < 0) continue;  // unsupported type: open only, no backfill

                    int32_t off = (int32_t)sl->getElementOffset((unsigned)mem.index);
                    llvm::Function* idxBackfill = ctx.getFunction(
                        "aiml_db_index_backfill",
                        ctx.builder.getVoidTy(), {i8p, i8p, i32, i32});
                    ctx.builder.CreateCall(idxBackfill,
                        {kvHandle, idxHandle, ctx.builder.getInt32(off), ctx.builder.getInt32(tag)});
                }
            }
            return ctx.ir_gen->wrap(kvHandle);
        };

        // db.open_vec("name", dims) → VecStore
        db.builtinFuncs["open_vec"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Value* nameVal = args.empty()
                ? ctx.builder.CreateGlobalStringPtr("")
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* dimsVal = args.size() >= 2
                ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
                : ctx.builder.getInt32(0);
            if (ctx.dbOpenPersist) {
                // path:nullptr → C-Resolver (resolve_aiondb_dir) nutzt
                // db.set_path / AIONDB_PATH / Fallback einheitlich.
                llvm::Value* pathVal = ctx.dbOpenBasePathVal
                    ? ctx.dbOpenBasePathVal
                    : llvm::ConstantPointerNull::get(
                          llvm::PointerType::getUnqual(ctx.context));
                llvm::Function* f = ctx.getFunction("aiml_db_vec_open_persist", i8p, {i8p, i32, i8p});
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {nameVal, dimsVal, pathVal}));
            }
            llvm::Function* f = ctx.getFunction("aiml_db_vec_open", i8p, {i8p, i32});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {nameVal, dimsVal}));
        };

        // db.open_timeseries("name") → TimeSeries<T>
        db.builtinFuncs["open_timeseries"] = [](CompilerContext& ctx,
                const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i64 = ctx.builder.getInt64Ty();
        llvm::Type* sz  = ctx.getSizeType();

            std::string targetType = ctx.deserializeTargetType;
            std::string elemName;
            if (targetType.rfind("TimeSeries<", 0) == 0 && targetType.back() == '>')
                elemName = targetType.substr(11, targetType.size() - 12);

            int64_t elemSize = 8;
            if (!elemName.empty() && ctx.classes.count(elemName)) {
                auto& ci = ctx.classes[elemName];
                if (ci.type) {
                    llvm::DataLayout dl(&ctx.module());
                    elemSize = static_cast<int64_t>(dl.getTypeAllocSize(ci.type));
                } else if (ci.packed_size > 0) {
                    elemSize = static_cast<int64_t>(ci.packed_size);
                }
            }

            llvm::Value* nameVal = args.empty()
                ? ctx.builder.CreateGlobalStringPtr("")
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            if (ctx.dbOpenPersist) {
                // path:nullptr → C-Resolver (resolve_aiondb_dir) nutzt
                // db.set_path / AIONDB_PATH / Fallback einheitlich.
                llvm::Value* pathVal = ctx.dbOpenBasePathVal
                    ? ctx.dbOpenBasePathVal
                    : llvm::ConstantPointerNull::get(
                          llvm::PointerType::getUnqual(ctx.context));
                llvm::Function* f = ctx.getFunction("aiml_db_ts_open_persist", i8p, {i8p, i64, i8p});
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(f,
                    {nameVal, ctx.builder.getInt64(elemSize), pathVal}));
            }
            llvm::Function* f = ctx.getFunction("aiml_db_ts_open", i8p, {i8p, i64});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f,
                {nameVal, ctx.builder.getInt64(elemSize)}));
        };

        // Stubs: begin / commit / rollback → return i32(0)
        auto stubFn = [](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
            return ctx.ir_gen->getInt32(0);
        };
        db.builtinFuncs["begin"]    = stubFn;
        db.builtinFuncs["commit"]   = stubFn;
        db.builtinFuncs["rollback"] = stubFn;

        // db.checkpoint → checkpoints all persistent stores
        db.builtinFuncs["checkpoint"] = [](CompilerContext& ctx,
                const std::vector<IRValue*>&) -> IRValue* {
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_db_checkpoint_all", i32, {});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {}));
        };

        // db.reload_kv(kv_handle) → void
        // Re-reads the WAL from disk so cross-process writes become visible.
        db.builtinFuncs["reload_kv"] = [](CompilerContext& ctx,
                const std::vector<IRValue*>& args) -> IRValue* {
            if (args.empty()) return ctx.ir_gen->getInt32(0);
            llvm::Type* i8p = llvm::PointerType::getUnqual(ctx.context);
            llvm::Function* f = ctx.getFunction("aiml_db_kv_reload", llvm::Type::getVoidTy(ctx.context), {i8p});
            llvm::Value* kv_ptr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            ctx.builder.CreateCall(f, {kv_ptr});
            return ctx.ir_gen->getInt32(0);
        };

        // db.set_path(path) → void
        // Sets the global AionDB root. Subsequent open_* calls with a
        // relative `path:` append their sub-path to this root.
        // Overridden by the AIONDB_PATH env var.
        db.builtinFuncs["set_path"] = [](CompilerContext& ctx,
                const std::vector<IRValue*>& args) -> IRValue* {
            if (args.empty()) return ctx.ir_gen->getInt32(0);
            llvm::Type* i8p = llvm::PointerType::getUnqual(ctx.context);
            llvm::Function* f = ctx.getFunction("aiml_db_set_path",
                                                llvm::Type::getVoidTy(ctx.context),
                                                {i8p});
            llvm::Value* path = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            ctx.builder.CreateCall(f, {path});
            return ctx.ir_gen->getInt32(0);
        };

        // db.get_path() → string
        // Returns the currently resolved AionDB root with a trailing slash.
        // Single source of truth — uses the same resolution as every
        // open_* call (env > set_path > "./aiondb/").
        db.builtinFuncs["get_path"] = [](CompilerContext& ctx,
                const std::vector<IRValue*>& /*args*/) -> IRValue* {
            llvm::Type* i8p = llvm::PointerType::getUnqual(ctx.context);
            llvm::Function* f = ctx.getFunction("aiml_db_get_path", i8p, {});
            llvm::Value* result = ctx.builder.CreateCall(f, {});
            return ctx.ir_gen->wrap(result);
        };

        // db.kv_evict(kv_handle) → void
        // Free in-memory map but keep WAL on disk. Use reload_kv to restore.
        db.builtinFuncs["kv_evict"] = [](CompilerContext& ctx,
                const std::vector<IRValue*>& args) -> IRValue* {
            if (args.empty()) return ctx.ir_gen->getInt32(0);
            llvm::Type* i8p = llvm::PointerType::getUnqual(ctx.context);
            llvm::Function* f = ctx.getFunction("aiml_db_kv_evict", llvm::Type::getVoidTy(ctx.context), {i8p});
            llvm::Value* kv_ptr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            ctx.builder.CreateCall(f, {kv_ptr});
            return ctx.ir_gen->getInt32(0);
        };

        // db.query(store, {where: fn}, {order_by: "field"}, {limit: N}, {offset: N})
        // → list<ptr<T>>
        // Option bags are pre-parsed into ctx.dbQuery* by visitCall before this handler runs.
        db.builtinFuncs["query"] = [](CompilerContext& ctx,
                const std::vector<IRValue*>& args) -> IRValue* {
            if (args.empty()) return ctx.ir_gen->getInt32(0);

            llvm::Type* i8p   = llvm::PointerType::getUnqual(ctx.context);
            llvm::Type* i32t  = ctx.builder.getInt32Ty();
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);

            llvm::Value* storeVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));

            // spec/31 §4.1: db.query works on a Store<T> (db.open).
            // A Kv (db.open_kv) has different internals -- the call
            // used to compile CLEANLY and then read nonsense: in the
            // DataCreatures server a SIGSEGV at startup
            // (aiml_db_store_is_active, addr=0x3f800000 -- the bit pattern of
            // 1.0f used as a pointer), in a small probe silently an
            // empty list.  Both are the same mix-up, and both
            // times it only shows up at runtime.
            if (ctx.dbQueryStoreType.rfind("Kv<", 0) == 0) {
                LOG_ERROR(ctx.fmtLoc() + "db.query expects a Store<T> (db.open), but it got "
                    + ctx.dbQueryStoreType + " (db.open_kv).  A Kv is iterated with `.iter` "
                    "or read with `.get` -- db.query does not see its entries.");
                return ctx.ir_gen->getInt32(0);
            }

            // Predicate function pointer (NULL if no where clause)
            llvm::Value* predPtr = llvm::ConstantPointerNull::get(
                llvm::cast<llvm::PointerType>(i8p));
            if (!ctx.dbQueryWhere.empty()) {
                llvm::Function* predFn = ctx.module().getFunction(ctx.dbQueryWhere);
                if (predFn) {
                    predPtr = ctx.builder.CreateBitCast(predFn, i8p, "pred_ptr");
                } else {
                    LOG_ERROR(ctx.fmtLoc() + "db.query: predicate function '" + ctx.dbQueryWhere + "' not found");
                }
            }

            // order_by: compute field offset + size from Store element type
            // (extracted from ctx.dbQueryStoreType "Store<ElemType>", set by visitCall pre-parse)
            int32_t ordOff = -1, ordSz = 0;
            if (!ctx.dbQueryOrderBy.empty() && !ctx.dbQueryStoreType.empty()) {
                std::string tgt = ctx.dbQueryStoreType;
                if (tgt.rfind("Store<", 0) == 0 && tgt.back() == '>') {
                    std::string elemN = tgt.substr(6, tgt.size() - 7);
                    if (ctx.classes.count(elemN) && ctx.classes[elemN].type) {
                        auto& ci = ctx.classes[elemN];
                        llvm::DataLayout dl(&ctx.module());
                        const llvm::StructLayout* sl = dl.getStructLayout(ci.type);
                        auto mit = ci.members.find(ctx.dbQueryOrderBy);
                        if (mit != ci.members.end()) {
                            ordOff = (int32_t)sl->getElementOffset(
                                (unsigned)mit->second.index);
                            ordSz  = (int32_t)dl.getTypeAllocSize(mit->second.type);
                        }
                    }
                }
            }

            // spec/31 §4.1 + spec/61 §5.4: the result is a weak list<ref<T>> of store borrows.
            const std::string& target = ctx.deserializeTargetType;
            if (target.rfind("list<ptr<", 0) == 0) {
                std::string inner = target.substr(9, target.size() - 11);
                LOG_ERROR(ctx.fmtLoc() + "db.query yields list<ref<" + inner + ">> (borrows into the store, "
                    "which keeps ownership); binding it to " + target + " would make every element a unique "
                    "owner of a store record. Use list<ref<" + inner + ">> (spec/31 §4.1, spec/61 §5.4).");
            }
            // Allocate result list
            llvm::StructType* listTy = ListImpl::getListStructType(ctx);
            llvm::Value* listAlloca  = ctx.memV3
                ? static_cast<llvm::Value*>(ctx.builder.CreateCall(ctx.getFunction("aiml_list_handle_new", i8p, {}), {}, "query_h"))
                : ctx.createEntryAlloca(listTy, "query_list");
            ListImpl::initList(ctx, listAlloca, ptrTy, 8);

            // Call C runtime
            llvm::Function* f = ctx.getFunction("aiml_db_store_query",
                ctx.builder.getVoidTy(),
                {i8p, i8p, i32t, i32t, i32t, i32t, i32t, ptrTy});
            ctx.builder.CreateCall(f, {
                storeVal, predPtr,
                ctx.builder.getInt32(ordOff),
                ctx.builder.getInt32(ordSz),
                ctx.builder.getInt32(ctx.dbQueryOrderDir),
                ctx.builder.getInt32(ctx.dbQueryLimit),
                ctx.builder.getInt32(ctx.dbQueryOffset),
                listAlloca
            });
            return ctx.ir_gen->wrap(listAlloca);
        };
    }

    // -----------------------------------------------------------------------
    // HTTP namespace + handle types
    // -----------------------------------------------------------------------
    {
        llvm::Type* i8p  = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32t = ctx.builder.getInt32Ty();
        llvm::Type* i8t  = ctx.builder.getInt8Ty();
        llvm::Type* voidT = ctx.builder.getVoidTy();

        // Helper: call a void* → i8* C function with a base ptr
        auto callGet = [&](const char* fnName, llvm::Value* base) -> IRValue* {
            llvm::Function* fn = ctx.module().getFunction(fnName);
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(i8p, {i8p}, false),
                llvm::Function::ExternalLinkage, fnName, ctx.module());
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base}));
        };
        auto callGetI32 = [&](const char* fnName, llvm::Value* base) -> IRValue* {
            llvm::Function* fn = ctx.module().getFunction(fnName);
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(i32t, {i8p}, false),
                llvm::Function::ExternalLinkage, fnName, ctx.module());
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base}));
        };
        auto callGetI8 = [&](const char* fnName, llvm::Value* base) -> IRValue* {
            llvm::Function* fn = ctx.module().getFunction(fnName);
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(i8t, {i8p}, false),
                llvm::Function::ExternalLinkage, fnName, ctx.module());
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base}));
        };

        // ── url ──────────────────────────────────────────────────────────
        // Convention: builtinMethod handlers receive the alloca (base); they load from
        // it to obtain the actual pointer before calling C runtime functions.
        {
            CompilerContext::ClassInfo ci;
            ci.name      = "url";
            ci.isBuiltin = true;
            // preLoadBase = false (default): URL handlers load from alloca internally.
// memV3 (spec/61 §4/§5): url parts are libc strings and get_param an AimlHttpMap;
// hand out managed copies (string / map<string:string> handle) — see
// aiml_http_str_managed / aiml_http_map_to_handle.  `managed` is the converter
// (to_string allocates a fresh libc string → adopted and freed).
#define URL_GETTER_STR(mName, cFn, managed) \
            ci.builtinMethods[mName] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* { \
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo(); \
                llvm::Value* ptr = ctx.builder.CreateLoad(i8p, base, "url_ptr"); \
                llvm::Function* fn = ctx.module().getFunction(cFn); \
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {i8p}, false), llvm::Function::ExternalLinkage, cFn, ctx.module()); \
                llvm::Value* raw = ctx.builder.CreateCall(fn, {ptr}); \
                const char* conv = managed; \
                if (ctx.memV3 && conv && !ctx.httpMapRawView) \
                    raw = ctx.builder.CreateCall(ctx.getFunction(conv, i8p, {i8p}), {raw}); \
                return ctx.ir_gen->wrap(raw); \
            }
            URL_GETTER_STR("protocol",  "aiml_url_protocol",  "aiml_http_str_managed");
            URL_GETTER_STR("host",      "aiml_url_host",      "aiml_http_str_managed");
            URL_GETTER_STR("path",      "aiml_url_path",      "aiml_http_str_managed");
            URL_GETTER_STR("fragment",  "aiml_url_fragment",  "aiml_http_str_managed");
            URL_GETTER_STR("get_param", "aiml_url_get_param", "aiml_http_map_to_handle");
            URL_GETTER_STR("to_string", "aiml_url_to_string", "aiml_http_str_adopt");
#undef URL_GETTER_STR
            ci.builtinMethods["port"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Value* ptr = ctx.builder.CreateLoad(i8p, base, "url_ptr");
                llvm::Function* fn = ctx.module().getFunction("aiml_url_port");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt32Ty(), {i8p}, false), llvm::Function::ExternalLinkage, "aiml_url_port", ctx.module());
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {ptr}));
            };
            ci.builtinMethods["is_https"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Value* ptr = ctx.builder.CreateLoad(i8p, base, "url_ptr");
                llvm::Function* fn = ctx.module().getFunction("aiml_url_is_https");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt8Ty(), {i8p}, false), llvm::Function::ExternalLinkage, "aiml_url_is_https", ctx.module());
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {ptr}));
            };
            ctx.classes["url"] = std::move(ci);
        }

        // ── http namespace functions ──────────────────────────────────────
        {
            auto& http = ctx.namespaces["http"];
            // http.fetch_text(url) → string  (one-shot GET body; WASM uses browser fetch)
            http.builtinFuncs["fetch_text"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module().getFunction("aiml_fetch_text");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {i8p}, false), llvm::Function::ExternalLinkage, "aiml_fetch_text", ctx.module());
                llvm::Value* url = args.empty()
                    ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p))
                    : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {url}));
            };
            // http.fetch(method, url, headers, body) → string (spec/32_2 F32-2-60…65;
            // WASM: window.fetch, native: http_client).  Missing arguments are "".
            http.builtinFuncs["fetch"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module().getFunction("aiml_fetch");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {i8p, i8p, i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_fetch", ctx.module());
                std::vector<llvm::Value*> call_args;
                for (size_t i = 0; i < 4; ++i) {
                    if (i < args.size()) call_args.push_back(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])));
                    else call_args.push_back(ctx.builder.CreateGlobalStringPtr(""));
                }
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, call_args));
            };
            // http.fetch_status() → i32: status of the last http.fetch on this thread, 0 = no answer
            http.builtinFuncs["fetch_status"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                (void)args;
                llvm::Function* fn = ctx.module().getFunction("aiml_fetch_status");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt32Ty(), {}, false), llvm::Function::ExternalLinkage, "aiml_fetch_status", ctx.module());
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {}));
            };
            // http.get(url) → http_response_handle
            http.builtinFuncs["get"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                if (args.empty()) return ctx.ir_gen->getInt32(0);
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module().getFunction("aiml_http_get");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_get", ctx.module());
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))}));
            };
            // http.post(url, body) → http_response_handle
            http.builtinFuncs["post"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                if (args.size() < 2) return ctx.ir_gen->getInt32(0);
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module().getFunction("aiml_http_post");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_post", ctx.module());
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])), static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))}));
            };
            // http.put(url, body) → http_response_handle
            http.builtinFuncs["put"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                if (args.size() < 2) return ctx.ir_gen->getInt32(0);
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module().getFunction("aiml_http_put");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_put", ctx.module());
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])), static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))}));
            };
            // http.delete(url) → http_response_handle
            http.builtinFuncs["delete"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                if (args.empty()) return ctx.ir_gen->getInt32(0);
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module().getFunction("aiml_http_delete");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_delete", ctx.module());
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))}));
            };
            // http.head(url) → http_response_handle
            http.builtinFuncs["head"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                if (args.empty()) return ctx.ir_gen->getInt32(0);
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module().getFunction("aiml_http_head");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_head", ctx.module());
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))}));
            };
            // http.last_error() → string: why the last request on this thread got
            // no answer (a null response); "" after an answered one (spec/32_2 §2.3a).
            http.builtinFuncs["last_error"] = [](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                return ownedStringResult(ctx, ctx.builder.CreateCall(ctx.getFunction("aiml_http_last_error", i8p, {}), {}));
            };
            // http.new_request(method, url) → http_client_handle
            http.builtinFuncs["new_request"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                if (args.size() < 2) return ctx.ir_gen->getInt32(0);
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module().getFunction("aiml_http_new_request");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_new_request", ctx.module());
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])), static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))}));
            };
            // http.parse_url(str) → url
            http.builtinFuncs["parse_url"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                if (args.empty()) return ctx.ir_gen->getInt32(0);
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module().getFunction("aiml_url_parse");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {i8p}, false), llvm::Function::ExternalLinkage, "aiml_url_parse", ctx.module());
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))}));
            };
            // http.server(host, port) → http_server_handle
            http.builtinFuncs["server"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                if (args.size() < 2) return ctx.ir_gen->getInt32(0);
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module().getFunction("aiml_http_server");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {i8p, ctx.builder.getInt32Ty()}, false), llvm::Function::ExternalLinkage, "aiml_http_server", ctx.module());
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])), static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))}));
            };
            // http.server_tls(host, port, cert, key) → http_server_handle
            http.builtinFuncs["server_tls"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                if (args.size() < 4) return ctx.ir_gen->getInt32(0);
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module().getFunction("aiml_http_server_tls");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {i8p, ctx.builder.getInt32Ty(), i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_server_tls", ctx.module());
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])),
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2])),
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[3]))
                }));
            };
            // http.fs_options() → fs_options
            http.builtinFuncs["fs_options"] = [](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module().getFunction("aiml_fs_options_new");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {}, false), llvm::Function::ExternalLinkage, "aiml_fs_options_new", ctx.module());
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {}));
            };
        }

        // ── http_server_handle ────────────────────────────────────────────
        {
            CompilerContext::ClassInfo ci;
            ci.name      = "http_server_handle";
            ci.isBuiltin = true;
            ci.preLoadBase = true;
            // srv.route(method, path, handler)
            ci.builtinMethods["route"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module().getFunction("aiml_http_server_route");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, i8p, i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_server_route", ctx.module());
                if (!call || call->args.size() < 3) return nullptr;
                IRValue* a0 = evalExpr(call->args[0].get());
                IRValue* a1 = evalExpr(call->args[1].get());
                // arg[2] is a function name → get its LLVM Function* and bitcast to i8*
                llvm::Value* handlerPtr = nullptr;
                if (auto* ve = dynamic_cast<AIML::AST::VariableExpr*>(call->args[2].get())) {
                    llvm::Function* hf = ctx.module().getFunction(ve->name);
                    if (hf) handlerPtr = ctx.builder.CreateBitCast(hf, i8p);
                }
                if (!handlerPtr) {
                    IRValue* a2 = evalExpr(call->args[2].get());
                    handlerPtr = a2 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a2)) : llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p));
                }
                ctx.builder.CreateCall(fn, {base,
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a0)),
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a1)),
                    handlerPtr});
                return nullptr;
            };
            // srv.use(middleware)
            ci.builtinMethods["use"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module().getFunction("aiml_http_server_use");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_server_use", ctx.module());
                if (!call || call->args.empty()) return nullptr;
                llvm::Value* mwPtr = nullptr;
                if (auto* ve = dynamic_cast<AIML::AST::VariableExpr*>(call->args[0].get()))
                    if (llvm::Function* mf = ctx.module().getFunction(ve->name))
                        mwPtr = ctx.builder.CreateBitCast(mf, i8p);
                if (!mwPtr) { IRValue* a = evalExpr(call->args[0].get()); mwPtr = a ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a)) : llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p)); }
                ctx.builder.CreateCall(fn, {base, mwPtr});
                return nullptr;
            };
            // srv.group(prefix, middleware?) → http_route_handle
            ci.builtinMethods["group"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module().getFunction("aiml_http_server_group");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {i8p, i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_server_group", ctx.module());
                if (!call || call->args.empty()) return ctx.ir_gen->getInt32(0);
                IRValue* prefix = evalExpr(call->args[0].get());
                llvm::Value* mwPtr = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p));
                if (call->args.size() > 1) {
                    if (auto* ve = dynamic_cast<AIML::AST::VariableExpr*>(call->args[1].get()))
                        if (llvm::Function* mf = ctx.module().getFunction(ve->name))
                            mwPtr = ctx.builder.CreateBitCast(mf, i8p);
                    if (mwPtr == llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p))) {
                        IRValue* a = evalExpr(call->args[1].get());
                        if (a) mwPtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a));
                    }
                }
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(prefix)), mwPtr}));
            };
            // srv.set_header(name, val)
            ci.builtinMethods["set_header"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module().getFunction("aiml_http_server_set_header");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_server_set_header", ctx.module());
                if (!call || call->args.size() < 2) return nullptr;
                IRValue* a0 = evalExpr(call->args[0].get());
                IRValue* a1 = evalExpr(call->args[1].get());
                ctx.builder.CreateCall(fn, {base, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a0)), static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a1))});
                return nullptr;
            };
            // srv.set_timeout(secs) — request-read timeout (slowloris defence)
            ci.builtinMethods["set_timeout"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Type* i32 = ctx.builder.getInt32Ty();
                llvm::Function* fn = ctx.module().getFunction("aiml_http_server_set_timeout");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, i32}, false), llvm::Function::ExternalLinkage, "aiml_http_server_set_timeout", ctx.module());
                if (!call || call->args.empty()) return nullptr;
                IRValue* a0 = evalExpr(call->args[0].get());
                ctx.builder.CreateCall(fn, {base, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a0))});
                return nullptr;
            };
            // srv.set_spool(dir, threshold, max) — bodies above `threshold` bytes go to a
            // temp file in `dir` (req.body_path) instead of memory; `max` caps them.
            ci.builtinMethods["set_spool"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Type* i64 = ctx.builder.getInt64Ty();
                llvm::Function* fn = ctx.getFunction("aiml_http_server_set_spool", ctx.builder.getVoidTy(), {i8p, i8p, i64, i64});
                if (!call || call->args.size() < 2) return nullptr;
                auto argI64 = [&](size_t i) -> llvm::Value* {
                    if (call->args.size() <= i) return llvm::ConstantInt::get(i64, 0);
                    llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[i].get())));
                    if (v->getType()->isIntegerTy() && !v->getType()->isIntegerTy(64)) v = ctx.builder.CreateSExt(v, i64);
                    return v;
                };
                llvm::Value* dir = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
                if (dir->getType() != i8p) dir = ctx.builder.CreateBitCast(dir, i8p);
                ctx.builder.CreateCall(fn, {base, dir, argI64(1), argI64(2)});
                return nullptr;
            };
            // srv.set_max_body(bytes) — request body cap (default 8 MiB, max ~2 GiB)
            ci.builtinMethods["set_max_body"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Type* i64 = ctx.builder.getInt64Ty();
                llvm::Function* fn = ctx.getFunction("aiml_http_server_set_max_body", ctx.builder.getVoidTy(), {i8p, i64});
                if (!call || call->args.empty()) return nullptr;
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
                if (v->getType()->isIntegerTy() && !v->getType()->isIntegerTy(64)) v = ctx.builder.CreateSExt(v, i64);
                ctx.builder.CreateCall(fn, {base, v});
                return nullptr;
            };
            // srv.secure_headers(hsts) — enable nosniff/X-Frame-Options/Referrer-Policy (+HSTS if hsts!=0)
            ci.builtinMethods["secure_headers"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Type* i32 = ctx.builder.getInt32Ty();
                llvm::Function* fn = ctx.module().getFunction("aiml_http_server_secure_headers");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, i32}, false), llvm::Function::ExternalLinkage, "aiml_http_server_secure_headers", ctx.module());
                llvm::Value* hsts = call && !call->args.empty()
                    ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())))
                    : ctx.builder.getInt32(0);
                ctx.builder.CreateCall(fn, {base, hsts});
                return nullptr;
            };
            // srv.set_max_connections(n) — cap concurrent handler threads (0 = unlimited)
            ci.builtinMethods["set_max_connections"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Type* i32 = ctx.builder.getInt32Ty();
                llvm::Function* fn = ctx.module().getFunction("aiml_http_server_set_max_connections");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, i32}, false), llvm::Function::ExternalLinkage, "aiml_http_server_set_max_connections", ctx.module());
                if (!call || call->args.empty()) return nullptr;
                IRValue* a0 = evalExpr(call->args[0].get());
                ctx.builder.CreateCall(fn, {base, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a0))});
                return nullptr;
            };
            // srv.set_rate_limit(max_per_window, window_secs) — per-IP fixed-window limit
            ci.builtinMethods["set_rate_limit"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Type* i32 = ctx.builder.getInt32Ty();
                llvm::Function* fn = ctx.module().getFunction("aiml_http_server_set_rate_limit");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, i32, i32}, false), llvm::Function::ExternalLinkage, "aiml_http_server_set_rate_limit", ctx.module());
                if (!call || call->args.size() < 2) return nullptr;
                IRValue* a0 = evalExpr(call->args[0].get());
                IRValue* a1 = evalExpr(call->args[1].get());
                ctx.builder.CreateCall(fn, {base,
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a0)),
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a1))});
                return nullptr;
            };
            // srv.require_client_cert(ca_path) — enable mTLS: require+verify a client
            // cert against the CA bundle PEM. Returns i32 (0 ok, -1 error). TLS server only.
            ci.builtinMethods["require_client_cert"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Type* i32 = ctx.builder.getInt32Ty();
                llvm::Function* fn = ctx.module().getFunction("aiml_http_server_require_client_cert");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i32, {i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_server_require_client_cert", ctx.module());
                if (!call || call->args.empty()) return nullptr;
                IRValue* a0 = evalExpr(call->args[0].get());
                llvm::Value* r = ctx.builder.CreateCall(fn, {base, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a0))});
                return ctx.ir_gen->wrap(r);
            };
            // srv.filesystem(prefix, dirs, opts) — serve static files
            ci.builtinMethods["filesystem"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module().getFunction("aiml_http_server_filesystem");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, i8p, i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_server_filesystem", ctx.module());
                if (!call || call->args.size() < 3) return nullptr;
                IRValue* a0 = evalExpr(call->args[0].get());
                // dirs is a list<path>. build_fs_route reads it as an AimlList {size,cap,data}.
                // spec/61 §5 (memV3): the list local is a HANDLE — its slot holds a POINTER to the
                // {size,cap,data} part, so the VALUE of the variable (evalExpr) IS the AimlList*.
                // Passing the alloca ADDRESS here (as the OFF path does) would hand build_fs_route a
                // pointer-to-pointer → it reads the handle pointer's bytes as size/data → empty/garbage
                // fs_dirs → every request 404s (test_http_range/traversal). OFF keeps the alloca path
                // (there the alloca IS the inline {size,cap,data} struct).
                llvm::Value* dirsPtr = nullptr;
                if (auto* vx = dynamic_cast<AIML::AST::VariableExpr*>(call->args[1].get())) {
                    Symbol* sym = nullptr;
                    CompilerContext::Scope* sc = ctx.currentScope;
                    while (sc) {
                        if (sc->variables.count(vx->name)) { sym = &sc->variables[vx->name]; break; }
                        sc = sc->parent;
                    }
                    if (!sym && ctx.symbol_table.count(vx->name)) sym = &ctx.symbol_table[vx->name];
                    if (sym) {
                        llvm::Value* av = sym->alloca_inst
                            ? static_cast<llvm::Value*>(sym->alloca_inst)
                            : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(sym->ir_alloca_inst));
                        if (ctx.memV3) {
                            // memV3: the slot holds a POINTER to the {size,cap,data} part — LOAD it so
                            // build_fs_route receives the AimlList*, not the slot address (which it would
                            // misread as size/data → empty fs_dirs → 404). OFF: the alloca IS the struct.
                            dirsPtr = ctx.builder.CreateLoad(i8p, av, "fs_dirs_handle");
                        } else {
                            dirsPtr = ctx.builder.CreateBitCast(av, i8p);
                        }
                    }
                }
                if (!dirsPtr) {
                    IRValue* a1 = evalExpr(call->args[1].get());
                    dirsPtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a1));
                }
                IRValue* a2 = evalExpr(call->args[2].get());
                ctx.builder.CreateCall(fn, {base,
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a0)),
                    dirsPtr,
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a2))});
                return nullptr;
            };
            // srv.start() — blocking
            ci.builtinMethods["start"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module().getFunction("aiml_http_server_start");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_server_start", ctx.module());
                ctx.builder.CreateCall(fn, {base});
                return nullptr;
            };
            // srv.start_async() → thread_handle
            ci.builtinMethods["start_async"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module().getFunction("aiml_http_server_start_async");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_server_start_async", ctx.module());
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base}));
            };
            // srv.stop()
            ci.builtinMethods["stop"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module().getFunction("aiml_http_server_stop");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_server_stop", ctx.module());
                ctx.builder.CreateCall(fn, {base});
                return nullptr;
            };
            ctx.classes["http_server_handle"] = std::move(ci);
        }

        // ── http_route_handle ─────────────────────────────────────────────
        {
            CompilerContext::ClassInfo ci;
            ci.name      = "http_route_handle";
            ci.isBuiltin = true;
            ci.preLoadBase = true;

            auto makeRouteMethod = [](const char* cFn, bool hasPrefix) {
                return [cFn, hasPrefix](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                    llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                    llvm::Function* fn = ctx.module().getFunction(cFn);
                    if (hasPrefix) {
                        // route(method, path, handler)
                        if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, i8p, i8p, i8p}, false), llvm::Function::ExternalLinkage, cFn, ctx.module());
                        if (!call || call->args.size() < 3) return nullptr;
                        IRValue* a0 = evalExpr(call->args[0].get());
                        IRValue* a1 = evalExpr(call->args[1].get());
                        llvm::Value* hPtr = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p));
                        if (auto* ve = dynamic_cast<AIML::AST::VariableExpr*>(call->args[2].get()))
                            if (llvm::Function* hf = ctx.module().getFunction(ve->name))
                                hPtr = ctx.builder.CreateBitCast(hf, i8p);
                        ctx.builder.CreateCall(fn, {base, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a0)), static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a1)), hPtr});
                    } else {
                        // use(middleware)
                        if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, i8p}, false), llvm::Function::ExternalLinkage, cFn, ctx.module());
                        if (!call || call->args.empty()) return nullptr;
                        llvm::Value* mwPtr = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p));
                        if (auto* ve = dynamic_cast<AIML::AST::VariableExpr*>(call->args[0].get()))
                            if (llvm::Function* mf = ctx.module().getFunction(ve->name))
                                mwPtr = ctx.builder.CreateBitCast(mf, i8p);
                        ctx.builder.CreateCall(fn, {base, mwPtr});
                    }
                    return nullptr;
                };
            };

            ci.builtinMethods["route"] = makeRouteMethod("aiml_http_route_route", true);
            ci.builtinMethods["use"]   = makeRouteMethod("aiml_http_route_use",   false);
            // group(prefix, middleware?) → http_route_handle
            ci.builtinMethods["group"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module().getFunction("aiml_http_route_group");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {i8p, i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_route_group", ctx.module());
                if (!call || call->args.empty()) return ctx.ir_gen->getInt32(0);
                IRValue* prefix = evalExpr(call->args[0].get());
                llvm::Value* mwPtr = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p));
                if (call->args.size() > 1) {
                    if (auto* ve = dynamic_cast<AIML::AST::VariableExpr*>(call->args[1].get()))
                        if (llvm::Function* mf = ctx.module().getFunction(ve->name))
                            mwPtr = ctx.builder.CreateBitCast(mf, i8p);
                }
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(prefix)), mwPtr}));
            };
            // filesystem(prefix, dirs, opts) — static files under a group
            ci.builtinMethods["filesystem"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module().getFunction("aiml_http_route_filesystem");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, i8p, i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_route_filesystem", ctx.module());
                if (!call || call->args.size() < 3) return nullptr;
                IRValue* a0 = evalExpr(call->args[0].get());
                IRValue* a1 = evalExpr(call->args[1].get());
                IRValue* a2 = evalExpr(call->args[2].get());
                ctx.builder.CreateCall(fn, {base,
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a0)),
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a1)),
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a2))});
                return nullptr;
            };
            ctx.classes["http_route_handle"] = std::move(ci);
        }

        // ── http_response_handle ──────────────────────────────────────────
        {
            CompilerContext::ClassInfo ci;
            ci.name      = "http_response_handle";
            ci.isBuiltin = true;
            ci.preLoadBase = true;
            // Property getters (zero-arg, called via resolveProperty → builtinMethods)
            ci.builtinMethods["status"]  = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Function* fn = ctx.module().getFunction("aiml_http_resp_status");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt32Ty(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "aiml_http_resp_status", ctx.module());
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base}));
            };
            ci.builtinMethods["ok"]      = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Function* fn = ctx.module().getFunction("aiml_http_resp_ok");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt8Ty(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "aiml_http_resp_ok", ctx.module());
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base}));
            };
            ci.builtinMethods["body"]    = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Function* fn = ctx.module().getFunction("aiml_http_resp_body");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "aiml_http_resp_body", ctx.module());
                return ownedStringResult(ctx, ctx.builder.CreateCall(fn, {base}));   // a fresh copy (aiml_str_dup)
            };
            // headers field (returns AimlHttpMap* for chained dispatch)
            ci.builtinMethods["headers"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Function* fn = ctx.module().getFunction("aiml_http_resp_get_headers");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "aiml_http_resp_get_headers", ctx.module());
                llvm::Value* raw = ctx.builder.CreateCall(fn, {base});
                // memV3: bound to a map<string:string> variable, the response's own
                // AimlHttpMap would be adopted by the variable — its drop freed the
                // map, and resp.free() freed it again (double free).  Outside a
                // chain (`resp.headers.put/get`, which works on the real map) hand
                // out a managed copy, as req.params / req.cookies do (spec/61 §5).
                if (ctx.memV3 && !ctx.httpMapRawView) {
                    llvm::Type* i8pc = ctx.builder.getInt8Ty()->getPointerTo();
                    raw = ctx.builder.CreateCall(ctx.getFunction("aiml_http_map_to_handle", i8pc, {i8pc}), {raw});
                }
                return ctx.ir_gen->wrap(raw);
            };
            // Client side: the URL that answered, after redirects (spec/32_2 §2.3a).
            ci.builtinMethods["url"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                return ownedStringResult(ctx, ctx.builder.CreateCall(ctx.getFunction("aiml_http_resp_url", i8p, {i8p}), {base}));
            };
            // Client side: exact byte length of the received body (binary-safe).
            ci.builtinMethods["body_len"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Function* fn = ctx.module().getFunction("aiml_http_resp_body_len");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt64Ty(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "aiml_http_resp_body_len", ctx.module());
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base}));
            };
            // Client side: resp.save_to(path) → i32 (0 ok, -1 error) — the received
            // body written byte-exact (staged + renamed).  `body` is a text view
            // and stops at the first NUL; binary downloads go through here.
            ci.builtinMethods["save_to"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module().getFunction("aiml_http_resp_save_to");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt32Ty(), {i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_resp_save_to", ctx.module());
                if (!call || call->args.empty()) return ctx.ir_gen->wrap(ctx.builder.getInt32(-1));
                IRValue* a = evalExpr(call->args[0].get());
                if (!a) return ctx.ir_gen->wrap(ctx.builder.getInt32(-1));
                llvm::Value* pv = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a));
                if (!pv->getType()->isPointerTy()) return ctx.ir_gen->wrap(ctx.builder.getInt32(-1));
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base, pv}));
            };
            // Methods: send, write, write_bytes, flush, close, free
            ci.builtinMethods["send"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Function* fn = ctx.module().getFunction("aiml_http_resp_send");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "aiml_http_resp_send", ctx.module());
                ctx.builder.CreateCall(fn, {base}); return nullptr;
            };
            ci.builtinMethods["write"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module().getFunction("aiml_http_resp_write");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_resp_write", ctx.module());
                if (call && !call->args.empty()) {
                    IRValue* a = evalExpr(call->args[0].get());
                    if (a) ctx.builder.CreateCall(fn, {base, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a))});
                }
                return nullptr;
            };
            ci.builtinMethods["flush"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Function* fn = ctx.module().getFunction("aiml_http_resp_flush");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "aiml_http_resp_flush", ctx.module());
                ctx.builder.CreateCall(fn, {base}); return nullptr;
            };
            ci.builtinMethods["close"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Function* fn = ctx.module().getFunction("aiml_http_resp_close");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "aiml_http_resp_close", ctx.module());
                ctx.builder.CreateCall(fn, {base}); return nullptr;
            };
            ci.builtinMethods["free"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Function* fn = ctx.module().getFunction("aiml_http_resp_free");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "aiml_http_resp_free", ctx.module());
                ctx.builder.CreateCall(fn, {base}); return nullptr;
            };
            // resp.set_cookie(name, value, max_age, same_site) — Set-Cookie with secure
            // defaults (Path=/, HttpOnly, Secure, SameSite). max_age<0 → session cookie;
            // same_site "" → "Lax". Multiple calls emit multiple Set-Cookie headers.
            ci.builtinMethods["set_cookie"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Type* i32 = ctx.builder.getInt32Ty();
                llvm::Function* fn = ctx.module().getFunction("aiml_http_resp_set_cookie");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, i8p, i8p, i32, i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_resp_set_cookie", ctx.module());
                if (!call || call->args.size() < 2) return nullptr;
                llvm::Value* nameA = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
                llvm::Value* valA  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
                llvm::Value* ageA  = call->args.size() > 2
                    ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[2].get())))
                    : llvm::ConstantInt::getSigned(i32, -1);
                if (ageA->getType()->isPointerTy()) ageA = ctx.builder.CreatePtrToInt(ageA, i32);
                llvm::Value* ssA = call->args.size() > 3
                    ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[3].get())))
                    : llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p));
                ctx.builder.CreateCall(fn, {base, nameA, valA, ageA, ssA});
                return nullptr;
            };
            // Property setters (called via visitSetStmt → builtinSetters)
            ci.builtinSetters["status"] = [](CompilerContext& ctx, llvm::Value* base, AIML::IRValue* newVal) {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module().getFunction("aiml_http_resp_set_status");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, ctx.builder.getInt32Ty()}, false), llvm::Function::ExternalLinkage, "aiml_http_resp_set_status", ctx.module());
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(newVal));
                if (v->getType()->isPointerTy()) v = ctx.builder.CreatePtrToInt(v, ctx.builder.getInt32Ty());
                ctx.builder.CreateCall(fn, {base, v});
            };
            ci.builtinSetters["body"] = [](CompilerContext& ctx, llvm::Value* base, AIML::IRValue* newVal) {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module().getFunction("aiml_http_resp_set_body");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_resp_set_body", ctx.module());
                ctx.builder.CreateCall(fn, {base, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(newVal))});
            };
            // resp.body_file = path — streamed (sendfile, Range-aware) on send
            ci.builtinSetters["body_file"] = [](CompilerContext& ctx, llvm::Value* base, AIML::IRValue* newVal) {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.getFunction("aiml_http_resp_set_body_file", ctx.builder.getVoidTy(), {i8p, i8p});
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(newVal));
                if (v->getType() != i8p) v = ctx.builder.CreateBitCast(v, i8p);
                ctx.builder.CreateCall(fn, {base, v});
            };
            ctx.classes["http_response_handle"] = std::move(ci);
        }

        // ── http_request_handle ───────────────────────────────────────────
        {
            CompilerContext::ClassInfo ci;
            ci.name      = "http_request_handle";
            ci.isBuiltin = true;
            ci.preLoadBase = true;
            // Property getters
            ci.builtinMethods["method"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Function* fn = ctx.module().getFunction("aiml_http_req_method");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "aiml_http_req_method", ctx.module());
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base}));
            };
            ci.builtinMethods["path"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Function* fn = ctx.module().getFunction("aiml_http_req_path");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "aiml_http_req_path", ctx.module());
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base}));
            };
            ci.builtinMethods["body"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Function* fn = ctx.module().getFunction("aiml_http_req_body");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "aiml_http_req_body", ctx.module());
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base}));
            };
            ci.builtinMethods["url"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Function* fn = ctx.module().getFunction("aiml_http_req_url");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "aiml_http_req_url", ctx.module());
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base}));
            };
            // params / cookies — AimlHttpMap* for chained dispatch (req.params.get(k))
            ci.builtinMethods["params"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Function* fn = ctx.module().getFunction("aiml_http_req_params");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "aiml_http_req_params", ctx.module());
                llvm::Value* raw = ctx.builder.CreateCall(fn, {base});
                // memV3: a map<string:string> is a refcounted handle with managed strings —
                // hand out a copy in that form, not the parser's AimlHttpMap (spec/61 §5).
                if (ctx.memV3 && !ctx.httpMapRawView) {
                    llvm::Type* i8pc = ctx.builder.getInt8Ty()->getPointerTo();
                    raw = ctx.builder.CreateCall(ctx.getFunction("aiml_http_map_to_handle", i8pc, {i8pc}), {raw});
                }
                return ctx.ir_gen->wrap(raw);
            };
            ci.builtinMethods["cookies"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Function* fn = ctx.module().getFunction("aiml_http_req_cookies");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "aiml_http_req_cookies", ctx.module());
                llvm::Value* raw = ctx.builder.CreateCall(fn, {base});
                // memV3: a map<string:string> is a refcounted handle with managed strings —
                // hand out a copy in that form, not the parser's AimlHttpMap (spec/61 §5).
                if (ctx.memV3 && !ctx.httpMapRawView) {
                    llvm::Type* i8pc = ctx.builder.getInt8Ty()->getPointerTo();
                    raw = ctx.builder.CreateCall(ctx.getFunction("aiml_http_map_to_handle", i8pc, {i8pc}), {raw});
                }
                return ctx.ir_gen->wrap(raw);
            };
            // req.header(name) — method with arg
            ci.builtinMethods["header"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module().getFunction("aiml_http_req_header");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_req_header", ctx.module());
                llvm::Value* nameArg = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p));
                if (call && !call->args.empty()) { IRValue* a = evalExpr(call->args[0].get()); if (a) nameArg = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a)); }
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base, nameArg}));
            };
            // req.cookie(name) — method with arg
            ci.builtinMethods["cookie"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module().getFunction("aiml_http_req_cookie");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_req_cookie", ctx.module());
                llvm::Value* nameArg = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p));
                if (call && !call->args.empty()) { IRValue* a = evalExpr(call->args[0].get()); if (a) nameArg = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a)); }
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base, nameArg}));
            };
            // req.body_bytes — raw request body as buffer<u8> (binary-safe, spec/32_2)
            ci.builtinMethods["body_bytes"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Type* bt  = ctx.getLLVMType("buffer");
                llvm::Function* fn  = ctx.getFunction("aiml_http_req_body_bytes", i8p, {i8p});
                llvm::Function* mcf = ctx.getFunction("memcpy", i8p, {i8p, i8p, ctx.getSizeType()});
                llvm::Function* frf = ctx.getFunction("free", ctx.builder.getVoidTy(), {i8p});
                llvm::Value* heapHdr = ctx.builder.CreateCall(fn, {base});
                uint64_t szBytes = ctx.module().getDataLayout().getTypeAllocSize(bt);
                if (ctx.memV3) {
                    // spec/61 §5: the runtime's heap header becomes an owned buffer
                    // handle (data moves along) — same as io.read_bytes.
                    llvm::Value* h = ctx.builder.CreateCall(ctx.getFunction("aiml_buffer_handle_new", i8p, {}), {}, "req_body_h");
                    llvm::Function* curFnV3 = ctx.builder.GetInsertBlock()->getParent();
                    auto* copyV3 = llvm::BasicBlock::Create(ctx.context, "req_body_copy", curFnV3);
                    auto* doneV3 = llvm::BasicBlock::Create(ctx.context, "req_body_done", curFnV3);
                    ctx.builder.CreateCondBr(ctx.builder.CreateIsNull(heapHdr), doneV3, copyV3);
                    ctx.builder.SetInsertPoint(copyV3);
                    ctx.builder.CreateCall(mcf, {h, heapHdr, ctx.getSizeConst(szBytes)});
                    ctx.builder.CreateCall(frf, {heapHdr});
                    ctx.builder.CreateBr(doneV3);
                    ctx.builder.SetInsertPoint(doneV3);
                    return ctx.ir_gen->wrap(h);
                }
                auto* out = ctx.createEntryAlloca(bt, "req_body_buf");
                ctx.builder.CreateMemSet(out, ctx.builder.getInt8(0), szBytes, llvm::MaybeAlign());
                // Copy the header only when the runtime returned one (OOM → zero buffer).
                llvm::Function* curFn = ctx.builder.GetInsertBlock()->getParent();
                auto* copyBB = llvm::BasicBlock::Create(ctx.context, "req_body_copy", curFn);
                auto* doneBB = llvm::BasicBlock::Create(ctx.context, "req_body_done", curFn);
                llvm::Value* isNull = ctx.builder.CreateIsNull(heapHdr);
                ctx.builder.CreateCondBr(isNull, doneBB, copyBB);
                ctx.builder.SetInsertPoint(copyBB);
                ctx.builder.CreateCall(mcf, {out, heapHdr, ctx.getSizeConst(szBytes)});
                ctx.builder.CreateCall(frf, {heapHdr});
                ctx.builder.CreateBr(doneBB);
                ctx.builder.SetInsertPoint(doneBB);
                return ctx.ir_gen->wrap(out);
            };
            // req.body_path — temp file of a spooled body (srv.set_spool) or null.
            ci.builtinMethods["body_path"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Value* raw = ctx.builder.CreateCall(ctx.getFunction("aiml_http_req_body_path", i8p, {i8p}), {base});
                // memV3: a managed string (the runtime returns a malloc'd copy → adopt it).
                if (ctx.memV3) raw = ctx.builder.CreateCall(ctx.getFunction("aiml_http_str_adopt", i8p, {i8p}), {raw});
                return ctx.ir_gen->wrap(raw);
            };
            // req.body_len — exact body length in bytes (i64; in memory or spooled).
            ci.builtinMethods["body_len"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(ctx.getFunction("aiml_http_req_body_len", ctx.builder.getInt64Ty(), {i8p}), {base}));
            };
            // req.client_ip() — trusted socket peer IP (ignores X-Forwarded-For)
            ci.builtinMethods["client_ip"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module().getFunction("aiml_http_req_client_ip");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_req_client_ip", ctx.module());
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base}));
            };
            ctx.classes["http_request_handle"] = std::move(ci);
        }

        // ── http_client_handle ────────────────────────────────────────────
        {
            CompilerContext::ClassInfo ci;
            ci.name      = "http_client_handle";
            ci.isBuiltin = true;
            ci.preLoadBase = true;
            // headers field → AimlHttpMap* for chained dispatch
            ci.builtinMethods["headers"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Function* fn = ctx.module().getFunction("aiml_http_client_get_headers");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "aiml_http_client_get_headers", ctx.module());
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base}));
            };
            // client.send() → http_response_handle
            ci.builtinMethods["send"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module().getFunction("aiml_http_client_send");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_client_send", ctx.module());
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base}));
            };
            // client.free() — release the request handle (after send; the response is
            // separate and freed with resp.free).  A second free is the caller's bug.
            ci.builtinMethods["free"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module().getFunction("aiml_http_client_free");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_client_free", ctx.module());
                ctx.builder.CreateCall(fn, {base});
                return ctx.ir_gen->getInt32(0);
            };
            // Property setters
            ci.builtinSetters["body"] = [](CompilerContext& ctx, llvm::Value* base, AIML::IRValue* newVal) {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module().getFunction("aiml_http_client_set_body");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_client_set_body", ctx.module());
                ctx.builder.CreateCall(fn, {base, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(newVal))});
            };
            // client.body_file = path — the request body is streamed from this
            // file at send time (binary-safe, no size limit, no heap copy).
            ci.builtinSetters["body_file"] = [](CompilerContext& ctx, llvm::Value* base, AIML::IRValue* newVal) {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module().getFunction("aiml_http_client_set_body_file");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_client_set_body_file", ctx.module());
                ctx.builder.CreateCall(fn, {base, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(newVal))});
            };
            ci.builtinSetters["timeout"] = [](CompilerContext& ctx, llvm::Value* base, AIML::IRValue* newVal) {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module().getFunction("aiml_http_client_set_timeout");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, ctx.builder.getInt32Ty()}, false), llvm::Function::ExternalLinkage, "aiml_http_client_set_timeout", ctx.module());
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(newVal));
                if (v->getType()->isPointerTy()) v = ctx.builder.CreatePtrToInt(v, ctx.builder.getInt32Ty());
                ctx.builder.CreateCall(fn, {base, v});
            };
            // spec/32_2 §2.3a: client.download_to / client.proxy (text) and
            // client.max_redirects (i32) — the runtime copies the text.
            for (const char* textSetter : {"download_to", "proxy"}) {
                const std::string sym = std::string("aiml_http_client_set_") + textSetter;
                ci.builtinSetters[textSetter] = [sym](CompilerContext& ctx, llvm::Value* base, AIML::IRValue* newVal) {
                    llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                    llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(newVal));
                    if (v->getType() != i8p) v = ctx.builder.CreateBitCast(v, i8p);
                    ctx.builder.CreateCall(ctx.getFunction(sym, ctx.builder.getVoidTy(), {i8p, i8p}), {base, v});
                };
            }
            ci.builtinSetters["max_redirects"] = [](CompilerContext& ctx, llvm::Value* base, AIML::IRValue* newVal) {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Type* i32 = ctx.builder.getInt32Ty();
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(newVal));
                if (v->getType()->isIntegerTy() && v->getType() != i32) v = ctx.builder.CreateIntCast(v, i32, true);
                ctx.builder.CreateCall(ctx.getFunction("aiml_http_client_set_max_redirects", ctx.builder.getVoidTy(), {i8p, i32}), {base, v});
            };
            ctx.classes["http_client_handle"] = std::move(ci);
        }

        // ── fs_options ────────────────────────────────────────────────────
        {
            CompilerContext::ClassInfo ci;
            ci.name      = "fs_options";
            ci.isBuiltin = true;
            ci.preLoadBase = true;
            ci.builtinSetters["index"] = [](CompilerContext& ctx, llvm::Value* base, AIML::IRValue* newVal) {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module().getFunction("aiml_fs_options_set_index");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_fs_options_set_index", ctx.module());
                ctx.builder.CreateCall(fn, {base, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(newVal))});
            };
            ci.builtinSetters["spa"] = [](CompilerContext& ctx, llvm::Value* base, AIML::IRValue* newVal) {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module().getFunction("aiml_fs_options_set_spa");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, ctx.builder.getInt8Ty()}, false), llvm::Function::ExternalLinkage, "aiml_fs_options_set_spa", ctx.module());
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(newVal));
                if (!v->getType()->isIntegerTy(8)) v = ctx.builder.CreateTrunc(v, ctx.builder.getInt8Ty());
                ctx.builder.CreateCall(fn, {base, v});
            };
            ci.builtinSetters["cache_max_age"] = [](CompilerContext& ctx, llvm::Value* base, AIML::IRValue* newVal) {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module().getFunction("aiml_fs_options_set_cache_max_age");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, ctx.builder.getInt32Ty()}, false), llvm::Function::ExternalLinkage, "aiml_fs_options_set_cache_max_age", ctx.module());
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(newVal));
                if (v->getType()->isPointerTy()) v = ctx.builder.CreatePtrToInt(v, ctx.builder.getInt32Ty());
                ctx.builder.CreateCall(fn, {base, v});
            };
            ctx.classes["fs_options"] = std::move(ci);
        }
    }

    // -----------------------------------------------------------------------
    // math namespace + math value types (vec2/vec3/vec4/quat/mat4/ivec2/ivec3)
    // -----------------------------------------------------------------------
    {
        llvm::Type* f32  = llvm::Type::getFloatTy(ctx.context);
        llvm::Type* i32  = llvm::Type::getInt32Ty(ctx.context);
        llvm::Type* i8p  = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* voidT = ctx.builder.getVoidTy();

        // Ensure struct types exist (getLLVMType has the same logic, call it to be safe)
        llvm::StructType* vec2Ty  = static_cast<llvm::StructType*>(ctx.getLLVMType("vec2"));
        llvm::StructType* vec3Ty  = static_cast<llvm::StructType*>(ctx.getLLVMType("vec3"));
        llvm::StructType* vec4Ty  = static_cast<llvm::StructType*>(ctx.getLLVMType("vec4"));
        llvm::StructType* quatTy  = static_cast<llvm::StructType*>(ctx.getLLVMType("quat"));
        llvm::StructType* mat4Ty  = static_cast<llvm::StructType*>(ctx.getLLVMType("mat4"));
        llvm::StructType* ivec2Ty = static_cast<llvm::StructType*>(ctx.getLLVMType("ivec2"));
        llvm::StructType* ivec3Ty = static_cast<llvm::StructType*>(ctx.getLLVMType("ivec3"));

        // ── Helper lambdas ────────────────────────────────────────────────

        // Store a loaded struct value to a temp alloca and return the alloca ptr
        auto storeToTmp = [](CompilerContext& ctx, llvm::Value* val, llvm::Type* ty) -> llvm::Value* {
            // a struct-returning method call arrives as the pointer to its result temp
            if (auto* res = llvm::dyn_cast<llvm::AllocaInst>(val))
                if (res->getAllocatedType() == ty) return res;
            llvm::AllocaInst* a = ctx.createEntryAlloca(ty, "mtmp");
            ctx.builder.CreateStore(val, a);
            return a;
        };

        // Get or declare a void C function with given pointer arg types
        auto getVoidFn = [&](const char* name, std::vector<llvm::Type*> args) -> llvm::Function* {
            llvm::Function* fn = ctx.module().getFunction(name);
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(voidT, args, false),
                llvm::Function::ExternalLinkage, name, ctx.module());
            return fn;
        };
        auto getF32Fn = [&](const char* name, std::vector<llvm::Type*> args) -> llvm::Function* {
            llvm::Function* fn = ctx.module().getFunction(name);
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(f32, args, false),
                llvm::Function::ExternalLinkage, name, ctx.module());
            return fn;
        };
        auto getI32Fn = [&](const char* name, std::vector<llvm::Type*> args) -> llvm::Function* {
            llvm::Function* fn = ctx.module().getFunction(name);
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(i32, args, false),
                llvm::Function::ExternalLinkage, name, ctx.module());
            return fn;
        };

        // Pointer types (opaque for passing structs by reference)
        llvm::Type* pVec2  = llvm::PointerType::get(ctx.context, 0);
        llvm::Type* pVec3  = llvm::PointerType::get(ctx.context, 0);
        llvm::Type* pVec4  = llvm::PointerType::get(ctx.context, 0);
        llvm::Type* pQuat  = llvm::PointerType::get(ctx.context, 0);
        llvm::Type* pMat4  = llvm::PointerType::get(ctx.context, 0);
        llvm::Type* pIVec2 = llvm::PointerType::get(ctx.context, 0);
        llvm::Type* pIVec3 = llvm::PointerType::get(ctx.context, 0);
        (void)pVec2; (void)pVec3; (void)pVec4; (void)pQuat; (void)pMat4;
        (void)pIVec2; (void)pIVec3;

        // ── Register ClassInfo for each math value type ───────────────────
        // Members are x→0, y→1, z→2, w→3 for vec types.
        // Registering as non-builtin with a real StructType so resolveProperty
        // uses the GEP path for member access (v.x, v.y, ...).
        // mat4 uses builtinMethods for get/set (no plain member access).

        auto addMember = [&](CompilerContext::ClassInfo& ci, const std::string& nm, int idx, llvm::Type* ty, const std::string& typNm) {
            CompilerContext::StructMember m;
            m.index    = idx;
            m.type     = ty;
            m.typeName = typNm;
            m.access   = AccessLevel::Public;
            ci.members[nm] = m;
        };

        // vec2 {f32, f32}
        {
            CompilerContext::ClassInfo ci;
            ci.name = "vec2"; ci.type = vec2Ty; ci.isBuiltin = false;
            addMember(ci, "x", 0, f32, "f32");
            addMember(ci, "y", 1, f32, "f32");
            ctx.classes["vec2"] = std::move(ci);
        }
        // vec3 {f32, f32, f32, f32}
        {
            CompilerContext::ClassInfo ci;
            ci.name = "vec3"; ci.type = vec3Ty; ci.isBuiltin = false;
            addMember(ci, "x", 0, f32, "f32");
            addMember(ci, "y", 1, f32, "f32");
            addMember(ci, "z", 2, f32, "f32");
            ctx.classes["vec3"] = std::move(ci);
        }
        // vec4 {f32, f32, f32, f32}
        {
            CompilerContext::ClassInfo ci;
            ci.name = "vec4"; ci.type = vec4Ty; ci.isBuiltin = false;
            addMember(ci, "x", 0, f32, "f32");
            addMember(ci, "y", 1, f32, "f32");
            addMember(ci, "z", 2, f32, "f32");
            addMember(ci, "w", 3, f32, "f32");
            ctx.classes["vec4"] = std::move(ci);
        }
        // quat {f32, f32, f32, f32}
        {
            CompilerContext::ClassInfo ci;
            ci.name = "quat"; ci.type = quatTy; ci.isBuiltin = false;
            addMember(ci, "x", 0, f32, "f32");
            addMember(ci, "y", 1, f32, "f32");
            addMember(ci, "z", 2, f32, "f32");
            addMember(ci, "w", 3, f32, "f32");
            ctx.classes["quat"] = std::move(ci);
        }
        // mat4 {[16 x f32]} — get/set methods only, no plain member access
        {
            CompilerContext::ClassInfo ci;
            ci.name = "mat4"; ci.type = mat4Ty; ci.isBuiltin = true;
            ci.preLoadBase = false;

            ci.builtinMethods["get"] = [mat4Ty](CompilerContext& ctx, llvm::Value* baseAlloca,
                                                  AIML::AST::CallExpr* call,
                                                  std::function<AIML::IRValue*(AIML::AST::Expr*)> evalExpr) -> AIML::IRValue* {
                // args: row (i32), col (i32)
                if (!call || call->args.size() < 2) return ctx.ir_gen->wrap(ctx.builder.CreateLoad(ctx.builder.getFloatTy(), baseAlloca, "m_get_err"));
                AIML::IRValue* rowV = evalExpr(call->args[0].get());
                AIML::IRValue* colV = evalExpr(call->args[1].get());
                llvm::Value* row = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(rowV));
                llvm::Value* col = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(colV));
                llvm::Function* fn = ctx.module().getFunction("aiml_math_mat4_get");
                if (!fn) fn = llvm::Function::Create(
                    llvm::FunctionType::get(ctx.builder.getFloatTy(),
                        {llvm::PointerType::get(ctx.context,0), ctx.builder.getInt32Ty(), ctx.builder.getInt32Ty()}, false),
                    llvm::Function::ExternalLinkage, "aiml_math_mat4_get", ctx.module());
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {baseAlloca, row, col}, "mat4_get"));
            };

            ci.builtinMethods["set"] = [mat4Ty](CompilerContext& ctx, llvm::Value* baseAlloca,
                                                  AIML::AST::CallExpr* call,
                                                  std::function<AIML::IRValue*(AIML::AST::Expr*)> evalExpr) -> AIML::IRValue* {
                // args: row (i32), col (i32), val (f32)
                if (!call || call->args.size() < 3) return ctx.ir_gen->getInt32(0);
                AIML::IRValue* rowV = evalExpr(call->args[0].get());
                AIML::IRValue* colV = evalExpr(call->args[1].get());
                AIML::IRValue* valV = evalExpr(call->args[2].get());
                llvm::Value* row = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(rowV));
                llvm::Value* col = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(colV));
                llvm::Value* val = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(valV));
                if (!val->getType()->isFloatTy())
                    val = ctx.builder.CreateSIToFP(val, ctx.builder.getFloatTy());
                llvm::Function* fn = ctx.module().getFunction("aiml_math_mat4_set");
                if (!fn) fn = llvm::Function::Create(
                    llvm::FunctionType::get(ctx.builder.getVoidTy(),
                        {llvm::PointerType::get(ctx.context,0), ctx.builder.getInt32Ty(), ctx.builder.getInt32Ty(), ctx.builder.getFloatTy()}, false),
                    llvm::Function::ExternalLinkage, "aiml_math_mat4_set", ctx.module());
                ctx.builder.CreateCall(fn, {baseAlloca, row, col, val});
                return ctx.ir_gen->getInt32(0);
            };

            ctx.classes["mat4"] = std::move(ci);
        }
        // ivec2 {i32, i32}
        {
            CompilerContext::ClassInfo ci;
            ci.name = "ivec2"; ci.type = ivec2Ty; ci.isBuiltin = false;
            addMember(ci, "x", 0, i32, "i32");
            addMember(ci, "y", 1, i32, "i32");
            ctx.classes["ivec2"] = std::move(ci);
        }
        // ivec3 {i32, i32, i32, i32}
        {
            CompilerContext::ClassInfo ci;
            ci.name = "ivec3"; ci.type = ivec3Ty; ci.isBuiltin = false;
            addMember(ci, "x", 0, i32, "i32");
            addMember(ci, "y", 1, i32, "i32");
            addMember(ci, "z", 2, i32, "i32");
            ctx.classes["ivec3"] = std::move(ci);
        }

        // ── math namespace functions ──────────────────────────────────────
        auto& math = ctx.namespaces["math"];

        // Helper: cast a loaded value to f32 (from f64 or integer literals)
        auto toF32 = [](CompilerContext& ctx, llvm::Value* v) -> llvm::Value* {
            if (v->getType()->isFloatTy()) return v;
            if (v->getType()->isDoubleTy()) return ctx.builder.CreateFPTrunc(v, ctx.builder.getFloatTy());
            if (v->getType()->isIntegerTy()) return ctx.builder.CreateSIToFP(v, ctx.builder.getFloatTy());
            return v;
        };

        // Constructors ─────────────────────────────────────────────────────
        math.builtinFuncs["vec2"] = [vec2Ty, toF32](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            llvm::Value* x = args.size()>0 ? toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))) : ctx.builder.CreateFPCast(llvm::ConstantFP::get(ctx.context, llvm::APFloat(0.0f)), ctx.builder.getFloatTy());
            llvm::Value* y = args.size()>1 ? toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))) : ctx.builder.CreateFPCast(llvm::ConstantFP::get(ctx.context, llvm::APFloat(0.0f)), ctx.builder.getFloatTy());
            llvm::Value* v = llvm::UndefValue::get(vec2Ty);
            v = ctx.builder.CreateInsertValue(v, x, 0);
            v = ctx.builder.CreateInsertValue(v, y, 1);
            return ctx.ir_gen->wrap(v);
        };
        math.builtinFuncs["vec3"] = [vec3Ty, toF32](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto zero = [&]{ return llvm::ConstantFP::get(ctx.builder.getFloatTy(), 0.0); };
            llvm::Value* x = args.size()>0 ? toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))) : zero();
            llvm::Value* y = args.size()>1 ? toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))) : zero();
            llvm::Value* z = args.size()>2 ? toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]))) : zero();
            llvm::Value* v = llvm::UndefValue::get(vec3Ty);
            v = ctx.builder.CreateInsertValue(v, x, 0);
            v = ctx.builder.CreateInsertValue(v, y, 1);
            v = ctx.builder.CreateInsertValue(v, z, 2);
            v = ctx.builder.CreateInsertValue(v, zero(), 3);
            return ctx.ir_gen->wrap(v);
        };
        math.builtinFuncs["vec4"] = [vec4Ty, toF32](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto zero = [&]{ return llvm::ConstantFP::get(ctx.builder.getFloatTy(), 0.0); };
            llvm::Value* x = args.size()>0 ? toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))) : zero();
            llvm::Value* y = args.size()>1 ? toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))) : zero();
            llvm::Value* z = args.size()>2 ? toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]))) : zero();
            llvm::Value* w = args.size()>3 ? toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[3]))) : zero();
            llvm::Value* v = llvm::UndefValue::get(vec4Ty);
            v = ctx.builder.CreateInsertValue(v, x, 0);
            v = ctx.builder.CreateInsertValue(v, y, 1);
            v = ctx.builder.CreateInsertValue(v, z, 2);
            v = ctx.builder.CreateInsertValue(v, w, 3);
            return ctx.ir_gen->wrap(v);
        };
        math.builtinFuncs["quat_identity"] = [quatTy](CompilerContext& ctx, const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            llvm::Function* fn = ctx.module().getFunction("aiml_math_quat_identity");
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(ctx.builder.getVoidTy(), {llvm::PointerType::get(ctx.context,0)}, false),
                llvm::Function::ExternalLinkage, "aiml_math_quat_identity", ctx.module());
            llvm::AllocaInst* out = ctx.createEntryAlloca(quatTy, "qi");
            ctx.builder.CreateCall(fn, {out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(quatTy, out, "qi_val"));
        };
        math.builtinFuncs["mat4_identity"] = [mat4Ty](CompilerContext& ctx, const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            llvm::Function* fn = ctx.module().getFunction("aiml_math_mat4_identity");
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(ctx.builder.getVoidTy(), {llvm::PointerType::get(ctx.context,0)}, false),
                llvm::Function::ExternalLinkage, "aiml_math_mat4_identity", ctx.module());
            llvm::AllocaInst* out = ctx.createEntryAlloca(mat4Ty, "mi");
            ctx.builder.CreateCall(fn, {out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(mat4Ty, out, "mi_val"));
        };
        math.builtinFuncs["ivec2"] = [ivec2Ty](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto getI32 = [&](int idx) -> llvm::Value* {
                if ((int)args.size() <= idx) return ctx.builder.getInt32(0);
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[idx]));
                if (!v->getType()->isIntegerTy(32)) v = ctx.builder.CreateSExtOrTrunc(v, ctx.builder.getInt32Ty());
                return v;
            };
            llvm::Value* v = llvm::UndefValue::get(ivec2Ty);
            v = ctx.builder.CreateInsertValue(v, getI32(0), 0);
            v = ctx.builder.CreateInsertValue(v, getI32(1), 1);
            return ctx.ir_gen->wrap(v);
        };
        math.builtinFuncs["ivec3"] = [ivec3Ty](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto getI32 = [&](int idx) -> llvm::Value* {
                if ((int)args.size() <= idx) return ctx.builder.getInt32(0);
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[idx]));
                if (!v->getType()->isIntegerTy(32)) v = ctx.builder.CreateSExtOrTrunc(v, ctx.builder.getInt32Ty());
                return v;
            };
            llvm::Value* v = llvm::UndefValue::get(ivec3Ty);
            v = ctx.builder.CreateInsertValue(v, getI32(0), 0);
            v = ctx.builder.CreateInsertValue(v, getI32(1), 1);
            v = ctx.builder.CreateInsertValue(v, getI32(2), 2);
            v = ctx.builder.CreateInsertValue(v, ctx.builder.getInt32(0), 3);
            return ctx.ir_gen->wrap(v);
        };

        // ── Scalar math (libm wrappers or LLVM intrinsics) ────────────────
        // to_radians / to_degrees
        math.builtinFuncs["to_radians"] = [toF32](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            llvm::Value* v = args.empty() ? llvm::ConstantFP::get(ctx.builder.getFloatTy(), 0.0)
                                          : toF32(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])));
            llvm::Value* pi = llvm::ConstantFP::get(ctx.builder.getFloatTy(), 3.14159265358979323846f);
            llvm::Value* d180 = llvm::ConstantFP::get(ctx.builder.getFloatTy(), 180.0f);
            return ctx.ir_gen->wrap(ctx.builder.CreateFMul(v, ctx.builder.CreateFDiv(pi, d180)));
        };
        math.builtinFuncs["to_degrees"] = [toF32](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            llvm::Value* v = args.empty() ? llvm::ConstantFP::get(ctx.builder.getFloatTy(), 0.0)
                                          : toF32(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])));
            llvm::Value* pi = llvm::ConstantFP::get(ctx.builder.getFloatTy(), 3.14159265358979323846f);
            llvm::Value* d180 = llvm::ConstantFP::get(ctx.builder.getFloatTy(), 180.0f);
            return ctx.ir_gen->wrap(ctx.builder.CreateFMul(v, ctx.builder.CreateFDiv(d180, pi)));
        };

        // Declare the libm functions once.  spec/43_0 §7: a function is computed in the WIDTH OF
        // ITS ARGUMENT — an f64 argument calls the double libm entry and yields f64, anything else
        // stays the f32 path.  Before this every scalar function went through `expf`/`logf`, so
        // `{ call: [math.exp, 1.0] }` into an f64 returned 2.71828174591064 instead of
        // 2.718281828459045: statistics and learning rates were silently f32.
        auto libmF1 = [&](const char* aimlName, const char* cName32, const char* cName64) {
            math.builtinFuncs[aimlName] = [cName32, cName64, toF32](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                llvm::Value* raw = args.empty() ? nullptr : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                // f64 when the argument is f64 OR the context asking for the result is
                // (spec/43_0 §7.1) — `decl: [v, f64, { call: [math.exp, 1.0] }]` must compute
                // in f64, and a bare literal argument carries no type of its own.
                bool wantWide = ctx.floatResultTy && ctx.floatResultTy->isDoubleTy();
                if ((raw && raw->getType()->isDoubleTy()) || (raw && wantWide)) {
                    llvm::Type* d = ctx.builder.getDoubleTy();
                    llvm::Function* fn = ctx.module().getFunction(cName64);
                    if (!fn) fn = llvm::Function::Create(
                        llvm::FunctionType::get(d, {d}, false),
                        llvm::Function::ExternalLinkage, cName64, ctx.module());
                    llvm::Value* wv = raw->getType()->isDoubleTy() ? raw
                                    : raw->getType()->isFloatingPointTy() ? ctx.builder.CreateFPExt(raw, d)
                                    : ctx.builder.CreateSIToFP(raw, d);
                    return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {wv}));
                }
                llvm::Function* fn = ctx.module().getFunction(cName32);
                if (!fn) fn = llvm::Function::Create(
                    llvm::FunctionType::get(ctx.builder.getFloatTy(), {ctx.builder.getFloatTy()}, false),
                    llvm::Function::ExternalLinkage, cName32, ctx.module());
                llvm::Value* v = raw ? toF32(ctx, raw) : llvm::ConstantFP::get(ctx.builder.getFloatTy(), 0.0);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {v}));
            };
        };
        libmF1("sin",   "sinf",   "sin");
        libmF1("cos",   "cosf",   "cos");
        libmF1("tan",   "tanf",   "tan");
        libmF1("asin",  "asinf",  "asin");
        libmF1("acos",  "acosf",  "acos");
        libmF1("sqrt",  "sqrtf",  "sqrt");
        libmF1("exp",   "expf",   "exp");
        libmF1("log",   "logf",   "log");
        libmF1("log2",  "log2f",  "log2");
        libmF1("floor", "floorf", "floor");
        libmF1("ceil",  "ceilf",  "ceil");
        libmF1("round", "roundf", "round");
        libmF1("abs",   "fabsf",  "fabs");

        // atan2 (2-arg)
        math.builtinFuncs["atan2"] = [toF32](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            // spec/43_0 §7: f64 arguments compute in f64 (an f64 in EITHER place is enough)
            auto rawOf = [&](int i) -> llvm::Value* {
                return args.size() > (size_t)i ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])) : nullptr;
            };
            bool wide = (rawOf(0) && rawOf(0)->getType()->isDoubleTy()) ||
                        (rawOf(1) && rawOf(1)->getType()->isDoubleTy()) ||
                        (ctx.floatResultTy && ctx.floatResultTy->isDoubleTy());   // spec/43_0 §7.1
            if (wide) {
                llvm::Type* d = ctx.builder.getDoubleTy();
                llvm::Function* fn = ctx.module().getFunction("atan2");
                if (!fn) fn = llvm::Function::Create(
                    llvm::FunctionType::get(d, {d, d}, false),
                    llvm::Function::ExternalLinkage, "atan2", ctx.module());
                auto gd = [&](int i) -> llvm::Value* {
                    llvm::Value* r = rawOf(i);
                    if (!r) return llvm::ConstantFP::get(d, 0.0);
                    if (r->getType()->isDoubleTy()) return r;
                    if (r->getType()->isFloatingPointTy()) return ctx.builder.CreateFPExt(r, d);
                    return ctx.builder.CreateSIToFP(r, d);
                };
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {gd(0), gd(1)}));
            }
            llvm::Function* fn = ctx.module().getFunction("atan2f");
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(ctx.builder.getFloatTy(),
                    {ctx.builder.getFloatTy(), ctx.builder.getFloatTy()}, false),
                llvm::Function::ExternalLinkage, "atan2f", ctx.module());
            auto g = [&](int i) { return args.size()>(size_t)i ? toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]))) : llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0); };
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {g(0), g(1)}));
        };

        // pow (2-arg)
        math.builtinFuncs["pow"] = [toF32](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            // spec/43_0 §7: f64 arguments compute in f64 (an f64 in EITHER place is enough)
            auto rawOf = [&](int i) -> llvm::Value* {
                return args.size() > (size_t)i ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])) : nullptr;
            };
            bool wide = (rawOf(0) && rawOf(0)->getType()->isDoubleTy()) ||
                        (rawOf(1) && rawOf(1)->getType()->isDoubleTy()) ||
                        (ctx.floatResultTy && ctx.floatResultTy->isDoubleTy());   // spec/43_0 §7.1
            if (wide) {
                llvm::Type* d = ctx.builder.getDoubleTy();
                llvm::Function* fn = ctx.module().getFunction("pow");
                if (!fn) fn = llvm::Function::Create(
                    llvm::FunctionType::get(d, {d, d}, false),
                    llvm::Function::ExternalLinkage, "pow", ctx.module());
                auto gd = [&](int i) -> llvm::Value* {
                    llvm::Value* r = rawOf(i);
                    if (!r) return llvm::ConstantFP::get(d, 0.0);
                    if (r->getType()->isDoubleTy()) return r;
                    if (r->getType()->isFloatingPointTy()) return ctx.builder.CreateFPExt(r, d);
                    return ctx.builder.CreateSIToFP(r, d);
                };
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {gd(0), gd(1)}));
            }
            llvm::Function* fn = ctx.module().getFunction("powf");
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(ctx.builder.getFloatTy(),
                    {ctx.builder.getFloatTy(), ctx.builder.getFloatTy()}, false),
                llvm::Function::ExternalLinkage, "powf", ctx.module());
            auto g = [&](int i) { return args.size()>(size_t)i ? toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]))) : llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0); };
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {g(0), g(1)}));
        };

        // fmod
        math.builtinFuncs["fmod"] = [toF32](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            // spec/43_0 §7: f64 arguments compute in f64 (an f64 in EITHER place is enough)
            auto rawOf = [&](int i) -> llvm::Value* {
                return args.size() > (size_t)i ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])) : nullptr;
            };
            bool wide = (rawOf(0) && rawOf(0)->getType()->isDoubleTy()) ||
                        (rawOf(1) && rawOf(1)->getType()->isDoubleTy()) ||
                        (ctx.floatResultTy && ctx.floatResultTy->isDoubleTy());   // spec/43_0 §7.1
            if (wide) {
                llvm::Type* d = ctx.builder.getDoubleTy();
                llvm::Function* fn = ctx.module().getFunction("fmod");
                if (!fn) fn = llvm::Function::Create(
                    llvm::FunctionType::get(d, {d, d}, false),
                    llvm::Function::ExternalLinkage, "fmod", ctx.module());
                auto gd = [&](int i) -> llvm::Value* {
                    llvm::Value* r = rawOf(i);
                    if (!r) return llvm::ConstantFP::get(d, 0.0);
                    if (r->getType()->isDoubleTy()) return r;
                    if (r->getType()->isFloatingPointTy()) return ctx.builder.CreateFPExt(r, d);
                    return ctx.builder.CreateSIToFP(r, d);
                };
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {gd(0), gd(1)}));
            }
            llvm::Function* fn = ctx.module().getFunction("fmodf");
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(ctx.builder.getFloatTy(),
                    {ctx.builder.getFloatTy(), ctx.builder.getFloatTy()}, false),
                llvm::Function::ExternalLinkage, "fmodf", ctx.module());
            auto g = [&](int i) { return args.size()>(size_t)i ? toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]))) : llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0); };
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {g(0), g(1)}));
        };

        // math.random(lo, hi) -> f32 — uniform in [lo, hi). Defaults lo=0, hi=1.
        math.builtinFuncs["random"] = [toF32](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            llvm::Function* fn = ctx.module().getFunction("aiml_math_random_f32");
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(ctx.builder.getFloatTy(),
                    {ctx.builder.getFloatTy(), ctx.builder.getFloatTy()}, false),
                llvm::Function::ExternalLinkage, "aiml_math_random_f32", ctx.module());
            llvm::Value* lo = args.size() > 0 ? toF32(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])))
                                              : llvm::ConstantFP::get(ctx.builder.getFloatTy(), 0.0);
            llvm::Value* hi = args.size() > 1 ? toF32(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])))
                                              : llvm::ConstantFP::get(ctx.builder.getFloatTy(), 1.0);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {lo, hi}));
        };

        // math.random_int(lo, hi) -> i32 — uniform integer in [lo, hi). Defaults lo=0.
        math.builtinFuncs["random_int"] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* i32t = ctx.builder.getInt32Ty();
            llvm::Function* fn = ctx.module().getFunction("aiml_math_random_int");
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(i32t, {i32t, i32t}, false),
                llvm::Function::ExternalLinkage, "aiml_math_random_int", ctx.module());
            auto toI32 = [&](llvm::Value* v) -> llvm::Value* {
                if (v->getType()->isIntegerTy(32)) return v;
                if (v->getType()->isIntegerTy(64)) return ctx.builder.CreateTrunc(v, i32t);
                if (v->getType()->isFloatingPointTy()) return ctx.builder.CreateFPToSI(v, i32t);
                return v;
            };
            llvm::Value* lo = args.size() > 0 ? toI32(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])))
                                              : llvm::ConstantInt::get(i32t, 0);
            llvm::Value* hi = args.size() > 1 ? toI32(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])))
                                              : llvm::ConstantInt::get(i32t, 0);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {lo, hi}));
        };

        // math.random_hex(nbytes: i32) → string (hex of cryptographically-secure random bytes, RAND_bytes)
        math.builtinFuncs["random_hex"] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            auto* i32t = ctx.builder.getInt32Ty();
            llvm::Function* fn = ctx.module().getFunction("aiml_math_random_hex");
            if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(p, {i32t}, false),
                                                 llvm::Function::ExternalLinkage, "aiml_math_random_hex", ctx.module());
            auto toI32 = [&](llvm::Value* v) -> llvm::Value* {
                if (v->getType()->isIntegerTy(32)) return v;
                if (v->getType()->isIntegerTy(64)) return ctx.builder.CreateTrunc(v, i32t);
                if (v->getType()->isFloatingPointTy()) return ctx.builder.CreateFPToSI(v, i32t);
                return v;
            };
            llvm::Value* n = args.size() > 0 ? toI32(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])))
                                             : llvm::ConstantInt::get(i32t, 16);
            // spec/61 §7: aiml_math_random_hex returns an OWNED refcounted string (to_hex_rc →
            // aiml_str_alloc). Register it as an owned string temp so a consuming decl/set MOVES it
            // (one owned ref, one drop) and a transient is decref'd at stmt end — else the initial
            // reference leaks (auth hash_password_new salt: 41 B/call, leak_probes/round3 §2).
            llvm::Value* rhres = ctx.builder.CreateCall(fn, {n});
            if (ctx.lifetimeDropsEnabled && !ctx.stmtTempStack.empty())
                AIML::AST::aimlTrackStringTemp(ctx, rhres);
            return ctx.ir_gen->wrap(rhres);
        };

        // fract = x - floor(x)
        math.builtinFuncs["fract"] = [toF32](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            llvm::Value* v = args.empty() ? llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0)
                                          : toF32(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])));
            llvm::Function* flFn = ctx.module().getFunction("floorf");
            if (!flFn) flFn = llvm::Function::Create(
                llvm::FunctionType::get(ctx.builder.getFloatTy(),{ctx.builder.getFloatTy()},false),
                llvm::Function::ExternalLinkage,"floorf",ctx.module());
            return ctx.ir_gen->wrap(ctx.builder.CreateFSub(v, ctx.builder.CreateCall(flFn,{v})));
        };

        // sign
        math.builtinFuncs["sign"] = [toF32](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            llvm::Value* v = args.empty() ? llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0)
                                          : toF32(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])));
            llvm::Value* pos = ctx.builder.CreateFCmpOGT(v, llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0));
            llvm::Value* neg = ctx.builder.CreateFCmpOLT(v, llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0));
            llvm::Value* p = ctx.builder.CreateUIToFP(pos, ctx.builder.getFloatTy());
            llvm::Value* n = ctx.builder.CreateUIToFP(neg, ctx.builder.getFloatTy());
            return ctx.ir_gen->wrap(ctx.builder.CreateFSub(p, n));
        };

        // min / max (f32)
        // Helper: emit call to aiml_math_fmin_list / aiml_math_fmax_list(float* data, i32 len)
        auto callListReduce = [](CompilerContext& ctx, const char* cName,
                                  llvm::Value* dataPtr, llvm::Value* len) -> llvm::Value* {
            auto* op = llvm::PointerType::get(ctx.context, 0);
            llvm::Function* fn = ctx.module().getFunction(cName);
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(ctx.builder.getFloatTy(),
                    {op, ctx.builder.getInt32Ty()}, false),
                llvm::Function::ExternalLinkage, cName, ctx.module());
            return ctx.builder.CreateCall(fn, {dataPtr, len});
        };
        // Helper: detect if a loaded value is a list struct {i32, i32, i8*}
        auto isListStruct = [](llvm::Type* ty) -> bool {
            auto* st = llvm::dyn_cast<llvm::StructType>(ty);
            return st && st->getNumElements() == 3 &&
                   st->getElementType(0)->isIntegerTy(32) &&
                   st->getElementType(1)->isIntegerTy(32) &&
                   st->getElementType(2)->isPointerTy();
        };

        math.builtinFuncs["min"] = [toF32, callListReduce, isListStruct]
            (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            if (args.empty()) return ctx.ir_gen->wrap(llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0));
            llvm::Value* a0 = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            // list<f32>: single struct arg {i32, i32, i8*}
            if (args.size() == 1 && isListStruct(a0->getType())) {
                llvm::Value* len  = ctx.builder.CreateExtractValue(a0, {0u});
                llvm::Value* data = ctx.builder.CreateExtractValue(a0, {2u});
                return ctx.ir_gen->wrap(callListReduce(ctx, "aiml_math_fmin_list", data, len));
            }
            // ptr+len: (i8*, i32)
            if (args.size() == 2) {
                llvm::Value* a1 = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
                if (a0->getType()->isPointerTy() && a1->getType()->isIntegerTy(32))
                    return ctx.ir_gen->wrap(callListReduce(ctx, "aiml_math_fmin_list", a0, a1));
            }
            // 2-scalar fallback
            auto g = [&](int i) { return args.size()>(size_t)i ? toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]))) : llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0); };
            llvm::Value* fa=g(0), *fb=g(1);
            return ctx.ir_gen->wrap(ctx.builder.CreateSelect(ctx.builder.CreateFCmpOLT(fa,fb), fa, fb));
        };
        math.builtinFuncs["max"] = [toF32, callListReduce, isListStruct]
            (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            if (args.empty()) return ctx.ir_gen->wrap(llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0));
            llvm::Value* a0 = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            // list<f32>: single struct arg {i32, i32, i8*}
            if (args.size() == 1 && isListStruct(a0->getType())) {
                llvm::Value* len  = ctx.builder.CreateExtractValue(a0, {0u});
                llvm::Value* data = ctx.builder.CreateExtractValue(a0, {2u});
                return ctx.ir_gen->wrap(callListReduce(ctx, "aiml_math_fmax_list", data, len));
            }
            // ptr+len: (i8*, i32)
            if (args.size() == 2) {
                llvm::Value* a1 = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
                if (a0->getType()->isPointerTy() && a1->getType()->isIntegerTy(32))
                    return ctx.ir_gen->wrap(callListReduce(ctx, "aiml_math_fmax_list", a0, a1));
            }
            // 2-scalar fallback
            auto g = [&](int i) { return args.size()>(size_t)i ? toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]))) : llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0); };
            llvm::Value* fa=g(0), *fb=g(1);
            return ctx.ir_gen->wrap(ctx.builder.CreateSelect(ctx.builder.CreateFCmpOGT(fa,fb), fa, fb));
        };

        // clamp(x, lo, hi)
        math.builtinFuncs["clamp"] = [toF32](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto g = [&](int i) { return args.size()>(size_t)i ? toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]))) : llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0); };
            llvm::Value* x=g(0), *lo=g(1), *hi=g(2);
            llvm::Value* clamped = ctx.builder.CreateSelect(ctx.builder.CreateFCmpOLT(x,lo), lo,
                                   ctx.builder.CreateSelect(ctx.builder.CreateFCmpOGT(x,hi), hi, x));
            return ctx.ir_gen->wrap(clamped);
        };

        // smoothstep(edge0, edge1, x)
        math.builtinFuncs["smoothstep"] = [toF32](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto g = [&](int i) { return args.size()>(size_t)i ? toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]))) : llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0); };
            llvm::Value* e0=g(0), *e1=g(1), *x=g(2);
            // t = clamp((x-e0)/(e1-e0), 0, 1)
            llvm::Value* range = ctx.builder.CreateFSub(e1, e0);
            llvm::Value* t = ctx.builder.CreateFDiv(ctx.builder.CreateFSub(x, e0), range);
            llvm::Value* zero = llvm::ConstantFP::get(ctx.builder.getFloatTy(), 0.0);
            llvm::Value* one  = llvm::ConstantFP::get(ctx.builder.getFloatTy(), 1.0);
            llvm::Value* two  = llvm::ConstantFP::get(ctx.builder.getFloatTy(), 2.0);
            llvm::Value* three= llvm::ConstantFP::get(ctx.builder.getFloatTy(), 3.0);
            t = ctx.builder.CreateSelect(ctx.builder.CreateFCmpOLT(t,zero),zero,
                ctx.builder.CreateSelect(ctx.builder.CreateFCmpOGT(t,one),one,t));
            // t*t*(3 - 2*t)
            llvm::Value* result = ctx.builder.CreateFMul(t, ctx.builder.CreateFMul(t,
                ctx.builder.CreateFSub(three, ctx.builder.CreateFMul(two, t))));
            return ctx.ir_gen->wrap(result);
        };

        // lerp scalar: lerp(a, b, t) = a + (b-a)*t
        math.builtinFuncs["lerp"] = [toF32, vec2Ty, vec3Ty, vec4Ty, quatTy, storeToTmp]
                                    (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            if (args.size() < 3) return ctx.ir_gen->getInt32(0);
            llvm::Value* a = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* b = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            llvm::Value* t = toF32(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2])));
            llvm::Type*  ty = a->getType();

            // Scalar f32
            if (ty->isFloatTy() || ty->isDoubleTy()) {
                llvm::Value* av = toF32(ctx,a); llvm::Value* bv = toF32(ctx,b);
                return ctx.ir_gen->wrap(ctx.builder.CreateFAdd(av, ctx.builder.CreateFMul(ctx.builder.CreateFSub(bv,av), t)));
            }
            // Vec types — call C runtime
            const char* fnName = nullptr;
            llvm::Type* structTy = nullptr;
            if (ty == vec2Ty) { fnName="aiml_math_vec2_lerp"; structTy=vec2Ty; }
            else if (ty == vec3Ty) { fnName="aiml_math_vec3_lerp"; structTy=vec3Ty; }
            else if (ty == vec4Ty) { fnName="aiml_math_vec4_lerp"; structTy=vec4Ty; }
            else if (ty == quatTy) { fnName="aiml_math_quat_lerp"; structTy=quatTy; }
            if (!fnName) return ctx.ir_gen->getInt32(0);
            auto opty = llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn = ctx.module().getFunction(fnName);
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(ctx.builder.getVoidTy(),{opty,opty,ctx.builder.getFloatTy(),opty},false),
                llvm::Function::ExternalLinkage, fnName, ctx.module());
            llvm::Value* pa = storeToTmp(ctx,a,structTy);
            llvm::Value* pb = storeToTmp(ctx,b,structTy);
            llvm::AllocaInst* out = ctx.createEntryAlloca(structTy);
            ctx.builder.CreateCall(fn,{pa,pb,t,out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(structTy,out));
        };

        // ── Vector functions ──────────────────────────────────────────────

        // dot (works for vec2, vec3)
        math.builtinFuncs["dot"] = [vec2Ty, vec3Ty, storeToTmp]
                                   (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            if (args.size() < 2) return ctx.ir_gen->wrap(llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0));
            llvm::Value* a = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* b = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            llvm::Type* ty = a->getType();
            const char* nm = (ty == vec2Ty) ? "aiml_math_vec2_dot" : "aiml_math_vec3_dot";
            llvm::Type* st = (ty == vec2Ty) ? vec2Ty : vec3Ty;
            auto op = llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn = ctx.module().getFunction(nm);
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(ctx.builder.getFloatTy(),{op,op},false),
                llvm::Function::ExternalLinkage, nm, ctx.module());
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn,{storeToTmp(ctx,a,st),storeToTmp(ctx,b,st)}));
        };

        // cross
        math.builtinFuncs["cross"] = [vec3Ty, storeToTmp]
                                     (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            if (args.size() < 2) return ctx.ir_gen->wrap(llvm::UndefValue::get(vec3Ty));
            auto op = llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn = ctx.module().getFunction("aiml_math_vec3_cross");
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,op,op},false),
                llvm::Function::ExternalLinkage, "aiml_math_vec3_cross", ctx.module());
            llvm::Value* a=static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* b=static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            llvm::AllocaInst* out=ctx.createEntryAlloca(vec3Ty);
            ctx.builder.CreateCall(fn,{storeToTmp(ctx,a,vec3Ty),storeToTmp(ctx,b,vec3Ty),out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(vec3Ty,out));
        };

        // length / length_sq / normalize / distance
        auto makeVecF = [&](const std::string& mathName, const char* c2, const char* c3, bool returnsVec,
                            llvm::StructType* fallbackSt, bool isScalar2arg = false) {
            math.builtinFuncs[mathName] = [c2,c3,returnsVec,fallbackSt,vec2Ty,vec3Ty,storeToTmp,isScalar2arg]
                (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                if (args.empty()) return ctx.ir_gen->wrap(llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0));
                llvm::Value* a=static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                llvm::Type* ty=a->getType();
                bool isV2 = (ty==vec2Ty);
                const char* nm = isV2 ? c2 : c3;
                llvm::Type* st = isV2 ? (llvm::Type*)vec2Ty : (llvm::Type*)vec3Ty;
                if (!nm) { nm=c3; st=fallbackSt; }
                auto op = llvm::PointerType::get(ctx.context,0);
                if (returnsVec) {
                    llvm::Function* fn=ctx.module().getFunction(nm);
                    if (!fn) {
                        std::vector<llvm::Type*> pts={op,op};
                        if (isScalar2arg) pts={op,ctx.builder.getFloatTy(),op};
                        fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),pts,false),
                            llvm::Function::ExternalLinkage,nm,ctx.module());
                    }
                    llvm::AllocaInst* out=ctx.createEntryAlloca(st);
                    if (isScalar2arg && args.size()>=2) {
                        llvm::Value* t2=static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
                        if (!t2->getType()->isFloatTy()) t2=ctx.builder.CreateSIToFP(t2,ctx.builder.getFloatTy());
                        ctx.builder.CreateCall(fn,{storeToTmp(ctx,a,st),t2,out});
                    } else {
                        llvm::Value* b=args.size()>1?storeToTmp(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])),st):nullptr;
                        if (b) ctx.builder.CreateCall(fn,{storeToTmp(ctx,a,st),b,out});
                        else   ctx.builder.CreateCall(fn,{storeToTmp(ctx,a,st),out});
                    }
                    return ctx.ir_gen->wrap(ctx.builder.CreateLoad(static_cast<llvm::Type*>(st),out));
                } else {
                    llvm::Function* fn=ctx.module().getFunction(nm);
                    if (!fn) {
                        std::vector<llvm::Type*> pts=(args.size()>1)?std::vector<llvm::Type*>{op,op}:std::vector<llvm::Type*>{op};
                        fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getFloatTy(),pts,false),
                            llvm::Function::ExternalLinkage,nm,ctx.module());
                    }
                    if (args.size()>1){
                        llvm::Value* b=storeToTmp(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])),st);
                        return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn,{storeToTmp(ctx,a,st),b}));
                    }
                    return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn,{storeToTmp(ctx,a,st)}));
                }
            };
        };

        makeVecF("length",    "aiml_math_vec2_length",    "aiml_math_vec3_length",    false, vec3Ty);
        makeVecF("length_sq", "aiml_math_vec2_length_sq", "aiml_math_vec3_length_sq", false, vec3Ty);
        makeVecF("normalize", "aiml_math_vec2_normalize", "aiml_math_vec3_normalize", true,  vec3Ty);
        makeVecF("distance",  "aiml_math_vec2_distance",  "aiml_math_vec3_distance",  false, vec3Ty);

        // angle (vec2 only)
        math.builtinFuncs["angle"] = [vec2Ty, storeToTmp]
                                     (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            if (args.empty()) return ctx.ir_gen->wrap(llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0));
            auto op=llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn=ctx.module().getFunction("aiml_math_vec2_angle");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getFloatTy(),{op},false),
                llvm::Function::ExternalLinkage,"aiml_math_vec2_angle",ctx.module());
            llvm::Value* a=static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn,{storeToTmp(ctx,a,vec2Ty)}));
        };
        // perp (vec2 only)
        math.builtinFuncs["perp"] = [vec2Ty, storeToTmp]
                                    (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            if (args.empty()) return ctx.ir_gen->wrap(llvm::UndefValue::get(vec2Ty));
            auto op=llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn=ctx.module().getFunction("aiml_math_vec2_perp");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,op},false),
                llvm::Function::ExternalLinkage,"aiml_math_vec2_perp",ctx.module());
            llvm::Value* a=static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::AllocaInst* out=ctx.createEntryAlloca(vec2Ty);
            ctx.builder.CreateCall(fn,{storeToTmp(ctx,a,vec2Ty),out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(vec2Ty,out));
        };

        // reflect / refract
        math.builtinFuncs["reflect"] = [vec3Ty, storeToTmp]
                                       (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            if (args.size()<2) return ctx.ir_gen->wrap(llvm::UndefValue::get(vec3Ty));
            auto op=llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn=ctx.module().getFunction("aiml_math_vec3_reflect");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,op,op},false),
                llvm::Function::ExternalLinkage,"aiml_math_vec3_reflect",ctx.module());
            llvm::Value* a=static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* b=static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            llvm::AllocaInst* out=ctx.createEntryAlloca(vec3Ty);
            ctx.builder.CreateCall(fn,{storeToTmp(ctx,a,vec3Ty),storeToTmp(ctx,b,vec3Ty),out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(vec3Ty,out));
        };
        math.builtinFuncs["refract"] = [vec3Ty, storeToTmp, toF32]
                                        (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            if (args.size()<3) return ctx.ir_gen->wrap(llvm::UndefValue::get(vec3Ty));
            auto op=llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn=ctx.module().getFunction("aiml_math_vec3_refract");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,op,ctx.builder.getFloatTy(),op},false),
                llvm::Function::ExternalLinkage,"aiml_math_vec3_refract",ctx.module());
            llvm::Value* a=static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* b=static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            llvm::Value* ior=toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2])));
            llvm::AllocaInst* out=ctx.createEntryAlloca(vec3Ty);
            ctx.builder.CreateCall(fn,{storeToTmp(ctx,a,vec3Ty),storeToTmp(ctx,b,vec3Ty),ior,out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(vec3Ty,out));
        };

        // ── Matrix functions ──────────────────────────────────────────────
        auto mathMat1V3Out = [&](const std::string& aimlNm, const char* cNm) {
            math.builtinFuncs[aimlNm] = [cNm, mat4Ty, vec3Ty, storeToTmp]
                (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                auto op=llvm::PointerType::get(ctx.context,0);
                llvm::Function* fn=ctx.module().getFunction(cNm);
                if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,op},false),
                    llvm::Function::ExternalLinkage,cNm,ctx.module());
                llvm::AllocaInst* out=ctx.createEntryAlloca(mat4Ty);
                if (!args.empty()) {
                    llvm::Value* v=static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                    ctx.builder.CreateCall(fn,{storeToTmp(ctx,v,vec3Ty),out});
                }
                return ctx.ir_gen->wrap(ctx.builder.CreateLoad(mat4Ty,out));
            };
        };
        mathMat1V3Out("mat4_translate", "aiml_math_mat4_translate");
        mathMat1V3Out("mat4_scale",     "aiml_math_mat4_scale");

        auto mathMatRotXYZ = [&](const std::string& aimlNm, const char* cNm) {
            math.builtinFuncs[aimlNm] = [cNm, mat4Ty, toF32]
                (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                auto op=llvm::PointerType::get(ctx.context,0);
                llvm::Function* fn=ctx.module().getFunction(cNm);
                if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{ctx.builder.getFloatTy(),op},false),
                    llvm::Function::ExternalLinkage,cNm,ctx.module());
                llvm::AllocaInst* out=ctx.createEntryAlloca(mat4Ty);
                llvm::Value* a=args.empty()?llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0):toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])));
                ctx.builder.CreateCall(fn,{a,out});
                return ctx.ir_gen->wrap(ctx.builder.CreateLoad(mat4Ty,out));
            };
        };
        mathMatRotXYZ("mat4_rotate_x", "aiml_math_mat4_rotate_x");
        mathMatRotXYZ("mat4_rotate_y", "aiml_math_mat4_rotate_y");
        mathMatRotXYZ("mat4_rotate_z", "aiml_math_mat4_rotate_z");

        // mat4_rotate(axis, angle)
        math.builtinFuncs["mat4_rotate"] = [mat4Ty, vec3Ty, storeToTmp, toF32]
            (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto op=llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn=ctx.module().getFunction("aiml_math_mat4_rotate");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,ctx.builder.getFloatTy(),op},false),
                llvm::Function::ExternalLinkage,"aiml_math_mat4_rotate",ctx.module());
            llvm::AllocaInst* out=ctx.createEntryAlloca(mat4Ty);
            llvm::Value* axis=args.size()>0?static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])):llvm::UndefValue::get(vec3Ty);
            llvm::Value* ang =args.size()>1?toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))):llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0);
            ctx.builder.CreateCall(fn,{storeToTmp(ctx,axis,vec3Ty),ang,out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(mat4Ty,out));
        };

        // mat4_perspective(fov, aspect, near, far)
        math.builtinFuncs["mat4_perspective"] = [mat4Ty, toF32]
            (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto op=llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn=ctx.module().getFunction("aiml_math_mat4_perspective");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),
                {ctx.builder.getFloatTy(),ctx.builder.getFloatTy(),ctx.builder.getFloatTy(),ctx.builder.getFloatTy(),op},false),
                llvm::Function::ExternalLinkage,"aiml_math_mat4_perspective",ctx.module());
            auto g=[&](int i){return args.size()>(size_t)i?toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]))):llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0);};
            llvm::AllocaInst* out=ctx.createEntryAlloca(mat4Ty);
            ctx.builder.CreateCall(fn,{g(0),g(1),g(2),g(3),out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(mat4Ty,out));
        };

        // mat4_ortho(l, r, b, t, near, far)
        math.builtinFuncs["mat4_ortho"] = [mat4Ty, toF32]
            (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto op=llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn=ctx.module().getFunction("aiml_math_mat4_ortho");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),
                {ctx.builder.getFloatTy(),ctx.builder.getFloatTy(),ctx.builder.getFloatTy(),ctx.builder.getFloatTy(),ctx.builder.getFloatTy(),ctx.builder.getFloatTy(),op},false),
                llvm::Function::ExternalLinkage,"aiml_math_mat4_ortho",ctx.module());
            auto g=[&](int i){return args.size()>(size_t)i?toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]))):llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0);};
            llvm::AllocaInst* out=ctx.createEntryAlloca(mat4Ty);
            ctx.builder.CreateCall(fn,{g(0),g(1),g(2),g(3),g(4),g(5),out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(mat4Ty,out));
        };

        // mat4_lookat(eye, center, up)
        math.builtinFuncs["mat4_lookat"] = [mat4Ty, vec3Ty, storeToTmp]
            (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto op=llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn=ctx.module().getFunction("aiml_math_mat4_lookat");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,op,op,op},false),
                llvm::Function::ExternalLinkage,"aiml_math_mat4_lookat",ctx.module());
            auto gv=[&](int i)->llvm::Value*{return args.size()>(size_t)i?static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])):llvm::UndefValue::get(vec3Ty);};
            llvm::AllocaInst* out=ctx.createEntryAlloca(mat4Ty);
            ctx.builder.CreateCall(fn,{storeToTmp(ctx,gv(0),vec3Ty),storeToTmp(ctx,gv(1),vec3Ty),storeToTmp(ctx,gv(2),vec3Ty),out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(mat4Ty,out));
        };

        // mat4_inverse / mat4_transpose
        auto mathMat1MatOut = [&](const std::string& aimlNm, const char* cNm) {
            math.builtinFuncs[aimlNm] = [cNm, mat4Ty, storeToTmp]
                (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                auto op=llvm::PointerType::get(ctx.context,0);
                llvm::Function* fn=ctx.module().getFunction(cNm);
                if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,op},false),
                    llvm::Function::ExternalLinkage,cNm,ctx.module());
                llvm::AllocaInst* out=ctx.createEntryAlloca(mat4Ty);
                if (!args.empty()) {
                    llvm::Value* m=static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                    ctx.builder.CreateCall(fn,{storeToTmp(ctx,m,mat4Ty),out});
                }
                return ctx.ir_gen->wrap(ctx.builder.CreateLoad(mat4Ty,out));
            };
        };
        mathMat1MatOut("mat4_inverse",   "aiml_math_mat4_inverse");
        mathMat1MatOut("mat4_transpose", "aiml_math_mat4_transpose");

        // mat4_to_buffer
        math.builtinFuncs["mat4_to_buffer"] = [mat4Ty, storeToTmp]
            (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto op=llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn=ctx.module().getFunction("aiml_math_mat4_to_buffer");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(op,{op},false),
                llvm::Function::ExternalLinkage,"aiml_math_mat4_to_buffer",ctx.module());
            if (args.empty()) return ctx.ir_gen->wrap(llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(op)));
            llvm::Value* m=static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn,{storeToTmp(ctx,m,mat4Ty)}));
        };

        // mat4_to_quat
        math.builtinFuncs["mat4_to_quat"] = [mat4Ty, quatTy, storeToTmp]
            (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto op=llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn=ctx.module().getFunction("aiml_math_mat4_to_quat");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,op},false),
                llvm::Function::ExternalLinkage,"aiml_math_mat4_to_quat",ctx.module());
            llvm::AllocaInst* out=ctx.createEntryAlloca(quatTy);
            if (!args.empty()) {
                llvm::Value* m=static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                ctx.builder.CreateCall(fn,{storeToTmp(ctx,m,mat4Ty),out});
            }
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(quatTy,out));
        };

        // ── Quaternion functions ──────────────────────────────────────────
        // quat_axis_angle(axis, angle)
        math.builtinFuncs["quat_axis_angle"] = [quatTy, vec3Ty, storeToTmp, toF32]
            (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto op=llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn=ctx.module().getFunction("aiml_math_quat_axis_angle");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,ctx.builder.getFloatTy(),op},false),
                llvm::Function::ExternalLinkage,"aiml_math_quat_axis_angle",ctx.module());
            llvm::AllocaInst* out=ctx.createEntryAlloca(quatTy);
            llvm::Value* axis=args.size()>0?static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])):llvm::UndefValue::get(vec3Ty);
            llvm::Value* ang =args.size()>1?toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))):llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0);
            ctx.builder.CreateCall(fn,{storeToTmp(ctx,axis,vec3Ty),ang,out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(quatTy,out));
        };

        // quat_euler(x, y, z)
        math.builtinFuncs["quat_euler"] = [quatTy, toF32]
            (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto op=llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn=ctx.module().getFunction("aiml_math_quat_euler");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),
                {ctx.builder.getFloatTy(),ctx.builder.getFloatTy(),ctx.builder.getFloatTy(),op},false),
                llvm::Function::ExternalLinkage,"aiml_math_quat_euler",ctx.module());
            auto g=[&](int i){return args.size()>(size_t)i?toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]))):llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0);};
            llvm::AllocaInst* out=ctx.createEntryAlloca(quatTy);
            ctx.builder.CreateCall(fn,{g(0),g(1),g(2),out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(quatTy,out));
        };

        // quat_mul / quat_normalize / quat_inverse / quat_to_mat4
        auto quatBin = [&](const std::string& nm, const char* cn) {
            math.builtinFuncs[nm] = [cn, quatTy, storeToTmp]
                (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                auto op=llvm::PointerType::get(ctx.context,0);
                llvm::Function* fn=ctx.module().getFunction(cn);
                if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,op,op},false),
                    llvm::Function::ExternalLinkage,cn,ctx.module());
                llvm::AllocaInst* out=ctx.createEntryAlloca(quatTy);
                llvm::Value* a=args.size()>0?static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])):llvm::UndefValue::get(quatTy);
                llvm::Value* b=args.size()>1?static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])):a;
                ctx.builder.CreateCall(fn,{storeToTmp(ctx,a,quatTy),storeToTmp(ctx,b,quatTy),out});
                return ctx.ir_gen->wrap(ctx.builder.CreateLoad(quatTy,out));
            };
        };
        auto quatUn = [&](const std::string& nm, const char* cn) {
            math.builtinFuncs[nm] = [cn, quatTy, storeToTmp]
                (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                auto op=llvm::PointerType::get(ctx.context,0);
                llvm::Function* fn=ctx.module().getFunction(cn);
                if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,op},false),
                    llvm::Function::ExternalLinkage,cn,ctx.module());
                llvm::AllocaInst* out=ctx.createEntryAlloca(quatTy);
                llvm::Value* a=args.size()>0?static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])):llvm::UndefValue::get(quatTy);
                ctx.builder.CreateCall(fn,{storeToTmp(ctx,a,quatTy),out});
                return ctx.ir_gen->wrap(ctx.builder.CreateLoad(quatTy,out));
            };
        };
        quatBin("quat_mul",  "aiml_math_quat_mul");
        quatUn ("quat_normalize", "aiml_math_quat_normalize");
        quatUn ("quat_inverse",   "aiml_math_quat_inverse");

        // quat_rotate_vec3(q, v)
        math.builtinFuncs["quat_rotate_vec3"] = [quatTy, vec3Ty, storeToTmp]
            (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto op=llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn=ctx.module().getFunction("aiml_math_quat_rotate_vec3");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,op,op},false),
                llvm::Function::ExternalLinkage,"aiml_math_quat_rotate_vec3",ctx.module());
            llvm::AllocaInst* out=ctx.createEntryAlloca(vec3Ty);
            llvm::Value* q=args.size()>0?static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])):llvm::UndefValue::get(quatTy);
            llvm::Value* v=args.size()>1?static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])):llvm::UndefValue::get(vec3Ty);
            ctx.builder.CreateCall(fn,{storeToTmp(ctx,q,quatTy),storeToTmp(ctx,v,vec3Ty),out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(vec3Ty,out));
        };

        // quat_to_mat4
        math.builtinFuncs["quat_to_mat4"] = [quatTy, mat4Ty, storeToTmp]
            (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto op=llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn=ctx.module().getFunction("aiml_math_quat_to_mat4");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,op},false),
                llvm::Function::ExternalLinkage,"aiml_math_quat_to_mat4",ctx.module());
            llvm::AllocaInst* out=ctx.createEntryAlloca(mat4Ty);
            llvm::Value* q=args.size()>0?static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])):llvm::UndefValue::get(quatTy);
            ctx.builder.CreateCall(fn,{storeToTmp(ctx,q,quatTy),out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(mat4Ty,out));
        };

        // quat_slerp
        math.builtinFuncs["quat_slerp"] = [quatTy, storeToTmp, toF32]
            (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto op=llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn=ctx.module().getFunction("aiml_math_quat_slerp");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,op,ctx.builder.getFloatTy(),op},false),
                llvm::Function::ExternalLinkage,"aiml_math_quat_slerp",ctx.module());
            llvm::AllocaInst* out=ctx.createEntryAlloca(quatTy);
            llvm::Value* a=args.size()>0?static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])):llvm::UndefValue::get(quatTy);
            llvm::Value* b=args.size()>1?static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])):a;
            llvm::Value* t=args.size()>2?toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]))):llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.5);
            ctx.builder.CreateCall(fn,{storeToTmp(ctx,a,quatTy),storeToTmp(ctx,b,quatTy),t,out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(quatTy,out));
        };

        // ── Integer vector ops ────────────────────────────────────────────
        math.builtinFuncs["idiv"] = [ivec2Ty, storeToTmp]
            (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto op=llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn=ctx.module().getFunction("aiml_math_ivec2_div");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,ctx.builder.getInt32Ty(),op},false),
                llvm::Function::ExternalLinkage,"aiml_math_ivec2_div",ctx.module());
            llvm::AllocaInst* out=ctx.createEntryAlloca(ivec2Ty);
            llvm::Value* a=args.size()>0?static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])):llvm::UndefValue::get(ivec2Ty);
            llvm::Value* s=args.size()>1?static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])):ctx.builder.getInt32(1);
            if (s->getType()->isFloatTy()||s->getType()->isDoubleTy()) s=ctx.builder.CreateFPToSI(s,ctx.builder.getInt32Ty());
            ctx.builder.CreateCall(fn,{storeToTmp(ctx,a,ivec2Ty),s,out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(ivec2Ty,out));
        };
        math.builtinFuncs["iadd"] = [ivec2Ty, storeToTmp]
            (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto op=llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn=ctx.module().getFunction("aiml_math_ivec2_add");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,op,op},false),
                llvm::Function::ExternalLinkage,"aiml_math_ivec2_add",ctx.module());
            llvm::AllocaInst* out=ctx.createEntryAlloca(ivec2Ty);
            llvm::Value* a=args.size()>0?static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])):llvm::UndefValue::get(ivec2Ty);
            llvm::Value* b=args.size()>1?static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])):a;
            ctx.builder.CreateCall(fn,{storeToTmp(ctx,a,ivec2Ty),storeToTmp(ctx,b,ivec2Ty),out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(ivec2Ty,out));
        };
        math.builtinFuncs["ivec2_to_vec2"] = [ivec2Ty, vec2Ty, storeToTmp]
            (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto op=llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn=ctx.module().getFunction("aiml_math_ivec2_to_vec2");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,op},false),
                llvm::Function::ExternalLinkage,"aiml_math_ivec2_to_vec2",ctx.module());
            llvm::AllocaInst* out=ctx.createEntryAlloca(vec2Ty);
            llvm::Value* a=args.size()>0?static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])):llvm::UndefValue::get(ivec2Ty);
            ctx.builder.CreateCall(fn,{storeToTmp(ctx,a,ivec2Ty),out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(vec2Ty,out));
        };
        math.builtinFuncs["vec2_to_ivec2"] = [ivec2Ty, vec2Ty, storeToTmp]
            (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto op=llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn=ctx.module().getFunction("aiml_math_vec2_to_ivec2");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,op},false),
                llvm::Function::ExternalLinkage,"aiml_math_vec2_to_ivec2",ctx.module());
            llvm::AllocaInst* out=ctx.createEntryAlloca(ivec2Ty);
            llvm::Value* a=args.size()>0?static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])):llvm::UndefValue::get(vec2Ty);
            ctx.builder.CreateCall(fn,{storeToTmp(ctx,a,vec2Ty),out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(ivec2Ty,out));
        };
    } // end math namespace

    // -----------------------------------------------------------------------
    // hash namespace
    // -----------------------------------------------------------------------
    {
        auto& hash = ctx.namespaces["hash"];
        auto* i32t = ctx.builder.getInt32Ty();

        /* Helper: declare or get (ptr→ptr) function for 1-arg hash functions */
        auto declHash1 = [&](const char* name) -> llvm::Function* {
            llvm::Function* fn = ctx.module().getFunction(name);
            if (!fn)
                fn = llvm::Function::Create(
                    llvm::FunctionType::get(llvm::PointerType::get(ctx.context,0),
                                            {llvm::PointerType::get(ctx.context,0)}, false),
                    llvm::Function::ExternalLinkage, name, ctx.module());
            return fn;
        };
        /* Helper: declare or get (ptr,ptr→ptr) function for 2-arg hash functions */
        auto declHash2 = [&](const char* name) -> llvm::Function* {
            llvm::Function* fn = ctx.module().getFunction(name);
            auto* p = llvm::PointerType::get(ctx.context,0);
            if (!fn)
                fn = llvm::Function::Create(
                    llvm::FunctionType::get(p, {p, p}, false),
                    llvm::Function::ExternalLinkage, name, ctx.module());
            return fn;
        };

#define REG_HASH1(aimlName, cName) \
        hash.builtinFuncs[aimlName] = [declHash1](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* { \
            auto* fn = declHash1(cName); \
            auto* p = llvm::PointerType::get(ctx.context,0); \
            llvm::Value* a = args.size() > 0 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])) \
                                              : llvm::Constant::getNullValue(p); \
            /* spec/61 §7: owned rc-string result → register as owned string temp (moved by a consuming */ \
            /* decl/set, decref'd at stmt end if transient) — else the initial ref leaks (hash.* surface). */ \
            llvm::Value* hres = ctx.builder.CreateCall(fn, {a}); \
            if (ctx.lifetimeDropsEnabled && !ctx.stmtTempStack.empty()) AIML::AST::aimlTrackStringTemp(ctx, hres); \
            return ctx.ir_gen->wrap(hres); \
        };

/* Buffer-aware hex digest: if the argument is a buffer<u8>, hash its raw bytes
 * (data ptr + size, binary-safe) via aiml_hash_hex_buf; otherwise the normal
 * null-terminated string path.  Keeps a single hash.<algo> surface — no per-type
 * or per-class hash methods. */
#define REG_HASH1B(aimlName, cName) \
        hash.builtinFuncs[aimlName] = [declHash1](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* { \
            auto* p   = llvm::PointerType::get(ctx.context, 0); \
            auto* i32 = ctx.builder.getInt32Ty(); \
            llvm::Value* a = args.size() > 0 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])) \
                                              : llvm::Constant::getNullValue(p); \
            if (a && a->getType() == ctx.getLLVMType("buffer")) { \
                llvm::Value* dataP = ctx.builder.CreateExtractValue(a, {4}, "hash_buf_data"); \
                llvm::Value* lenV  = ctx.builder.CreateExtractValue(a, {0}, "hash_buf_len"); \
                llvm::Function* bf = ctx.module().getFunction("aiml_hash_hex_buf"); \
                if (!bf) bf = llvm::Function::Create( \
                    llvm::FunctionType::get(p, {p, p, i32}, false), \
                    llvm::Function::ExternalLinkage, "aiml_hash_hex_buf", ctx.module()); \
                llvm::Value* algo = ctx.builder.CreateGlobalStringPtr(aimlName); \
                llvm::Value* hbres = ctx.builder.CreateCall(bf, {algo, dataP, lenV}); \
                if (ctx.lifetimeDropsEnabled && !ctx.stmtTempStack.empty()) AIML::AST::aimlTrackStringTemp(ctx, hbres); \
                return ctx.ir_gen->wrap(hbres); \
            } \
            auto* fn = declHash1(cName); \
            /* spec/61 §7: owned rc-string → register as owned string temp (round3 §1/§2 leak class). */ \
            llvm::Value* hsres = ctx.builder.CreateCall(fn, {a}); \
            if (ctx.lifetimeDropsEnabled && !ctx.stmtTempStack.empty()) AIML::AST::aimlTrackStringTemp(ctx, hsres); \
            return ctx.ir_gen->wrap(hsres); \
        };

#define REG_HASH2(aimlName, cName) \
        hash.builtinFuncs[aimlName] = [declHash2](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* { \
            auto* fn = declHash2(cName); \
            auto* p = llvm::PointerType::get(ctx.context, 0); \
            llvm::Value* a0 = args.size() > 0 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])) \
                                               : llvm::Constant::getNullValue(p); \
            llvm::Value* a1 = args.size() > 1 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])) \
                                               : llvm::Constant::getNullValue(p); \
            /* spec/61 §7: owned rc-string → register as owned string temp (round3 §1/§2 leak class). */ \
            llvm::Value* h2res = ctx.builder.CreateCall(fn, {a0, a1}); \
            if (ctx.lifetimeDropsEnabled && !ctx.stmtTempStack.empty()) AIML::AST::aimlTrackStringTemp(ctx, h2res); \
            return ctx.ir_gen->wrap(h2res); \
        };

        REG_HASH1B("sha256",       "aiml_hash_sha256")
        /* hash.sha256_file(path) → string: streams the file (1 MiB blocks), so that
         * large artefacts can be content-addressed without a RAM peak. */
        REG_HASH1("sha256_file",   "aiml_sha256_file")
        REG_HASH1B("sha512",       "aiml_hash_sha512")
        REG_HASH1B("sha3_256",     "aiml_hash_sha3_256")
        REG_HASH1("keccak256",     "aiml_hash_keccak256")
        REG_HASH1("ripemd160",     "aiml_hash_ripemd160")
        REG_HASH1B("blake2b",      "aiml_hash_blake2b")
        REG_HASH1B("blake2s",      "aiml_hash_blake2s")
        REG_HASH1B("md5",          "aiml_hash_md5")
        REG_HASH1B("sha1",         "aiml_hash_sha1")   /* git object ids (spec/68) */
        REG_HASH1("sha256_raw",    "aiml_hash_sha256_raw")
        REG_HASH1("sha512_raw",    "aiml_hash_sha512_raw")
        REG_HASH1("sha3_256_raw",  "aiml_hash_sha3_256_raw")
        REG_HASH1("keccak256_raw", "aiml_hash_keccak256_raw")
        REG_HASH1("ripemd160_raw", "aiml_hash_ripemd160_raw")
        REG_HASH1("blake2b_raw",   "aiml_hash_blake2b_raw")
        REG_HASH2("hmac_sha256",      "aiml_hash_hmac_sha256")
        REG_HASH2("hmac_sha512",      "aiml_hash_hmac_sha512")
        REG_HASH2("hmac_sha256_raw",  "aiml_hash_hmac_sha256_raw")

        // hash.const_time_eq(a: string, b: string) → i32 (1 = equal, constant-time)
        hash.builtinFuncs["const_time_eq"] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Function* fn = ctx.module().getFunction("aiml_hash_const_time_eq");
            if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i32, {p, p}, false),
                                                 llvm::Function::ExternalLinkage, "aiml_hash_const_time_eq", ctx.module());
            llvm::Value* a0 = args.size() > 0 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])) : llvm::Constant::getNullValue(p);
            llvm::Value* a1 = args.size() > 1 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])) : llvm::Constant::getNullValue(p);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {a0, a1}));
        };
        // hash.pbkdf2(password: string, salt: string, iterations: i32) → string (hex)
        hash.builtinFuncs["pbkdf2"] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Function* fn = ctx.module().getFunction("aiml_hash_pbkdf2");
            if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(p, {p, p, i32}, false),
                                                 llvm::Function::ExternalLinkage, "aiml_hash_pbkdf2", ctx.module());
            llvm::Value* a0 = args.size() > 0 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])) : llvm::Constant::getNullValue(p);
            llvm::Value* a1 = args.size() > 1 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])) : llvm::Constant::getNullValue(p);
            llvm::Value* a2 = args.size() > 2 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2])) : ctx.builder.getInt32(0);
            // spec/61 §7: aiml_hash_pbkdf2 returns an OWNED refcounted string (to_hex_rc). Register it
            // as an owned string temp (see math.random_hex) so a consuming decl/set MOVES it — else the
            // derived-key reference leaks (auth hash_password_new/verify_password: 73 B/call, round3 §2).
            llvm::Value* pkres = ctx.builder.CreateCall(fn, {a0, a1, a2});
            if (ctx.lifetimeDropsEnabled && !ctx.stmtTempStack.empty())
                AIML::AST::aimlTrackStringTemp(ctx, pkres);
            return ctx.ir_gen->wrap(pkres);
        };

#undef REG_HASH1
#undef REG_HASH2

        /* crc32: returns i32 */
        hash.builtinFuncs["crc32"] = [i32t](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            llvm::Function* fn = ctx.module().getFunction("aiml_hash_crc32");
            if (!fn)
                fn = llvm::Function::Create(
                    llvm::FunctionType::get(i32t, {p}, false),
                    llvm::Function::ExternalLinkage, "aiml_hash_crc32", ctx.module());
            llvm::Value* a = args.size() > 0 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))
                                             : llvm::Constant::getNullValue(p);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {a}));
        };
    } // end hash namespace

    // ── bytes namespace (spec/68) ─────────────────────────────────────────
    // Binary helpers on buffer<u8>.  Runtime: src/sys/bytes_runtime.cpp.
    // A buffer argument is passed as (data, len) taken from the buffer value; a
    // string argument as (chars, strlen).  A returned buffer is a malloc'd
    // header moved into a buffer value (memV3: a fresh handle), exactly like
    // base64.decode / io.read_bytes.  Returned strings are managed (tracked as
    // statement temps under lifetime drops, like hash.<algo>).
    {
        auto& bns = ctx.namespaces["bytes"];
        struct BytesHelp {
            // (data ptr, i32 len) of argument i — buffer or string.
            static std::pair<llvm::Value*, llvm::Value*> view(CompilerContext& ctx, const std::vector<IRValue*>& args, size_t i) {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Type* i32 = ctx.builder.getInt32Ty();
                if (args.size() <= i || !args[i])
                    return { llvm::Constant::getNullValue(i8p), ctx.builder.getInt32(0) };
                llvm::Value* a = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]));
                if (a->getType() == ctx.getLLVMType("buffer"))
                    return { ctx.builder.CreateExtractValue(a, {4}, "bytes_data"),
                             ctx.builder.CreateExtractValue(a, {0}, "bytes_len") };
                if (a->getType()->isPointerTy()) {
                    llvm::Value* n = ctx.builder.CreateCall(ctx.getFunction("strlen", ctx.getSizeType(), {i8p}), {a});
                    return { a, ctx.builder.CreateTrunc(n, i32, "str_len") };
                }
                return { llvm::Constant::getNullValue(i8p), ctx.builder.getInt32(0) };
            }
            static llvm::Value* i64arg(CompilerContext& ctx, const std::vector<IRValue*>& args, size_t i, int64_t dflt) {
                llvm::Type* i64 = ctx.builder.getInt64Ty();
                if (args.size() <= i || !args[i]) return ctx.builder.getInt64(dflt);
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]));
                if (v->getType()->isIntegerTy()) return ctx.builder.CreateSExtOrTrunc(v, i64);
                if (v->getType()->isFloatingPointTy()) return ctx.builder.CreateFPToSI(v, i64);
                return ctx.builder.getInt64(dflt);
            }
            static llvm::Value* i32arg(CompilerContext& ctx, const std::vector<IRValue*>& args, size_t i, int32_t dflt) {
                return ctx.builder.CreateTrunc(i64arg(ctx, args, i, dflt), ctx.builder.getInt32Ty());
            }
            // Moves a runtime header into a buffer value (memV3: owned handle).
            static IRValue* buf(CompilerContext& ctx, llvm::Value* hdr) {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Type* bt  = ctx.getLLVMType("buffer");
                llvm::Function* mcf = ctx.getFunction("memcpy", i8p, {i8p, i8p, ctx.getSizeType()});
                llvm::Function* frf = ctx.getFunction("free", ctx.builder.getVoidTy(), {i8p});
                uint64_t szBytes = ctx.module().getDataLayout().getTypeAllocSize(bt);
                llvm::Value* out;
                if (ctx.memV3)
                    out = ctx.builder.CreateCall(ctx.getFunction("aiml_buffer_handle_new", i8p, {}), {}, "bytes_buf_h");
                else
                    out = ctx.createEntryAlloca(bt, "bytes_buf");
                ctx.builder.CreateCall(mcf, {out, hdr, ctx.getSizeConst(szBytes)});
                ctx.builder.CreateCall(frf, {hdr});
                return ctx.ir_gen->wrap(out);
            }
            static IRValue* str(CompilerContext& ctx, llvm::Value* s) {
                if (ctx.lifetimeDropsEnabled && !ctx.stmtTempStack.empty())
                    AIML::AST::aimlTrackStringTemp(ctx, s);
                return ctx.ir_gen->wrap(s);
            }
        };
        using BF = std::vector<IRValue*>;
        // bytes.slice(buf, off, len = -1) → buffer<u8>
        bns.builtinFuncs["slice"] = [](CompilerContext& ctx, const BF& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo(); llvm::Type* i32 = ctx.builder.getInt32Ty(); llvm::Type* i64 = ctx.builder.getInt64Ty();
            auto v = BytesHelp::view(ctx, args, 0);
            return BytesHelp::buf(ctx, ctx.builder.CreateCall(ctx.getFunction("aiml_bytes_slice", i8p, {i8p, i32, i64, i64}),
                {v.first, v.second, BytesHelp::i64arg(ctx, args, 1, 0), BytesHelp::i64arg(ctx, args, 2, -1)}));
        };
        // bytes.concat(a, b) → buffer<u8>
        bns.builtinFuncs["concat"] = [](CompilerContext& ctx, const BF& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo(); llvm::Type* i32 = ctx.builder.getInt32Ty();
            auto a = BytesHelp::view(ctx, args, 0); auto b = BytesHelp::view(ctx, args, 1);
            return BytesHelp::buf(ctx, ctx.builder.CreateCall(ctx.getFunction("aiml_bytes_concat", i8p, {i8p, i32, i8p, i32}),
                {a.first, a.second, b.first, b.second}));
        };
        // bytes.u8(buf, idx) → i32 (0..255, -1 outside)
        bns.builtinFuncs["u8"] = [](CompilerContext& ctx, const BF& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo(); llvm::Type* i32 = ctx.builder.getInt32Ty(); llvm::Type* i64 = ctx.builder.getInt64Ty();
            auto v = BytesHelp::view(ctx, args, 0);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(ctx.getFunction("aiml_bytes_u8", i32, {i8p, i32, i64}),
                {v.first, v.second, BytesHelp::i64arg(ctx, args, 1, 0)}));
        };
        // bytes.find(buf, byte, from = 0) → i64 (-1 = none)
        bns.builtinFuncs["find"] = [](CompilerContext& ctx, const BF& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo(); llvm::Type* i32 = ctx.builder.getInt32Ty(); llvm::Type* i64 = ctx.builder.getInt64Ty();
            auto v = BytesHelp::view(ctx, args, 0);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(ctx.getFunction("aiml_bytes_find", i64, {i8p, i32, i32, i64}),
                {v.first, v.second, BytesHelp::i32arg(ctx, args, 1, 0), BytesHelp::i64arg(ctx, args, 2, 0)}));
        };
        // bytes.be32(buf, off) → i64 (unsigned big-endian, -1 outside)
        bns.builtinFuncs["be32"] = [](CompilerContext& ctx, const BF& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo(); llvm::Type* i32 = ctx.builder.getInt32Ty(); llvm::Type* i64 = ctx.builder.getInt64Ty();
            auto v = BytesHelp::view(ctx, args, 0);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(ctx.getFunction("aiml_bytes_be32", i64, {i8p, i32, i64}),
                {v.first, v.second, BytesHelp::i64arg(ctx, args, 1, 0)}));
        };
        // bytes.from_be32(value) / bytes.from_u8(value) → buffer<u8>
        bns.builtinFuncs["from_be32"] = [](CompilerContext& ctx, const BF& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo(); llvm::Type* i64 = ctx.builder.getInt64Ty();
            return BytesHelp::buf(ctx, ctx.builder.CreateCall(ctx.getFunction("aiml_bytes_from_be32", i8p, {i64}), {BytesHelp::i64arg(ctx, args, 0, 0)}));
        };
        bns.builtinFuncs["from_u8"] = [](CompilerContext& ctx, const BF& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo(); llvm::Type* i32 = ctx.builder.getInt32Ty();
            return BytesHelp::buf(ctx, ctx.builder.CreateCall(ctx.getFunction("aiml_bytes_from_u8", i8p, {i32}), {BytesHelp::i32arg(ctx, args, 0, 0)}));
        };
        // bytes.to_hex(buf, off = 0, len = -1) → string
        bns.builtinFuncs["to_hex"] = [](CompilerContext& ctx, const BF& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo(); llvm::Type* i32 = ctx.builder.getInt32Ty(); llvm::Type* i64 = ctx.builder.getInt64Ty();
            auto v = BytesHelp::view(ctx, args, 0);
            return BytesHelp::str(ctx, ctx.builder.CreateCall(ctx.getFunction("aiml_bytes_to_hex", i8p, {i8p, i32, i64, i64}),
                {v.first, v.second, BytesHelp::i64arg(ctx, args, 1, 0), BytesHelp::i64arg(ctx, args, 2, -1)}));
        };
        // bytes.text(buf, off = 0, len = -1) → string (stops at a NUL)
        bns.builtinFuncs["text"] = [](CompilerContext& ctx, const BF& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo(); llvm::Type* i32 = ctx.builder.getInt32Ty(); llvm::Type* i64 = ctx.builder.getInt64Ty();
            auto v = BytesHelp::view(ctx, args, 0);
            return BytesHelp::str(ctx, ctx.builder.CreateCall(ctx.getFunction("aiml_bytes_text", i8p, {i8p, i32, i64, i64}),
                {v.first, v.second, BytesHelp::i64arg(ctx, args, 1, 0), BytesHelp::i64arg(ctx, args, 2, -1)}));
        };
        // bytes.from_hex(text) / bytes.from_string(text) / bytes.read_file(path) → buffer<u8>
        auto regStrToBuf = [](const char* aimlName, const char* cName) {
            return [cName](CompilerContext& ctx, const BF& args) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Value* s = args.empty() ? static_cast<llvm::Value*>(ctx.builder.CreateGlobalStringPtr(""))
                                              : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                if (!s->getType()->isPointerTy()) s = ctx.builder.CreateGlobalStringPtr("");
                return BytesHelp::buf(ctx, ctx.builder.CreateCall(ctx.getFunction(cName, i8p, {i8p}), {s}));
            };
            (void)aimlName;
        };
        bns.builtinFuncs["from_hex"]    = regStrToBuf("from_hex", "aiml_bytes_from_hex");
        bns.builtinFuncs["from_string"] = regStrToBuf("from_string", "aiml_bytes_from_string");
        bns.builtinFuncs["read_file"]   = regStrToBuf("read_file", "aiml_bytes_read_file");
        // bytes.zlib(buf, level = 0) → buffer<u8> (zlib stream of exactly these bytes)
        bns.builtinFuncs["zlib"] = [](CompilerContext& ctx, const BF& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo(); llvm::Type* i32 = ctx.builder.getInt32Ty();
            auto v = BytesHelp::view(ctx, args, 0);
            return BytesHelp::buf(ctx, ctx.builder.CreateCall(ctx.getFunction("aiml_bytes_zlib", i8p, {i8p, i32, i32}),
                {v.first, v.second, BytesHelp::i32arg(ctx, args, 1, 0)}));
        };
        // bytes.zlib_at(buf, off) → buffer<u8>: inflate the ONE stream at off;
        // bytes.zlib_consumed() → i64 input bytes it took (-1 after an error).
        bns.builtinFuncs["zlib_at"] = [](CompilerContext& ctx, const BF& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo(); llvm::Type* i32 = ctx.builder.getInt32Ty(); llvm::Type* i64 = ctx.builder.getInt64Ty();
            auto v = BytesHelp::view(ctx, args, 0);
            return BytesHelp::buf(ctx, ctx.builder.CreateCall(ctx.getFunction("aiml_bytes_zlib_at", i8p, {i8p, i32, i64}),
                {v.first, v.second, BytesHelp::i64arg(ctx, args, 1, 0)}));
        };
        // ── file streaming (spec/68 addendum): large archives without loading them ──
        auto cstrArg = [](CompilerContext& ctx, const BF& args, size_t i) -> llvm::Value* {
            if (args.size() > i && args[i]) {
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]));
                if (v->getType()->isPointerTy()) return v;
            }
            return ctx.builder.CreateGlobalStringPtr("");
        };
        // bytes.append_file(path, buf) → i64
        bns.builtinFuncs["append_file"] = [cstrArg](CompilerContext& ctx, const BF& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo(); llvm::Type* i32 = ctx.builder.getInt32Ty(); llvm::Type* i64 = ctx.builder.getInt64Ty();
            auto v = BytesHelp::view(ctx, args, 1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(ctx.getFunction("aiml_bytes_append_file", i64, {i8p, i8p, i32}),
                {cstrArg(ctx, args, 0), v.first, v.second}));
        };
        // bytes.file_slice(path, off, len) → buffer<u8>
        bns.builtinFuncs["file_slice"] = [cstrArg](CompilerContext& ctx, const BF& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo(); llvm::Type* i64 = ctx.builder.getInt64Ty();
            return BytesHelp::buf(ctx, ctx.builder.CreateCall(ctx.getFunction("aiml_bytes_file_slice", i8p, {i8p, i64, i64}),
                {cstrArg(ctx, args, 0), BytesHelp::i64arg(ctx, args, 1, 0), BytesHelp::i64arg(ctx, args, 2, 0)}));
        };
        // bytes.copy_range(src, off, len, dst) → i64
        bns.builtinFuncs["copy_range"] = [cstrArg](CompilerContext& ctx, const BF& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo(); llvm::Type* i64 = ctx.builder.getInt64Ty();
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(ctx.getFunction("aiml_bytes_copy_range", i64, {i8p, i64, i64, i8p}),
                {cstrArg(ctx, args, 0), BytesHelp::i64arg(ctx, args, 1, 0), BytesHelp::i64arg(ctx, args, 2, -1), cstrArg(ctx, args, 3)}));
        };
        // bytes.gzip_file(src, dst, level = 6) → i64
        bns.builtinFuncs["gzip_file"] = [cstrArg](CompilerContext& ctx, const BF& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo(); llvm::Type* i32 = ctx.builder.getInt32Ty(); llvm::Type* i64 = ctx.builder.getInt64Ty();
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(ctx.getFunction("aiml_bytes_gzip_file", i64, {i8p, i8p, i32}),
                {cstrArg(ctx, args, 0), cstrArg(ctx, args, 1), BytesHelp::i32arg(ctx, args, 2, 6)}));
        };
        // bytes.gunzip_file(src, dst) → i64
        bns.builtinFuncs["gunzip_file"] = [cstrArg](CompilerContext& ctx, const BF& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo(); llvm::Type* i64 = ctx.builder.getInt64Ty();
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(ctx.getFunction("aiml_bytes_gunzip_file", i64, {i8p, i8p}),
                {cstrArg(ctx, args, 0), cstrArg(ctx, args, 1)}));
        };
        bns.builtinFuncs["zlib_consumed"] = [](CompilerContext& ctx, const BF&) -> IRValue* {
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(ctx.getFunction("aiml_bytes_zlib_consumed", ctx.builder.getInt64Ty(), {}), {}));
        };
        // bytes.crc32(buf, off = 0, len = -1) → i64 (unsigned CRC-32, -1 outside)
        bns.builtinFuncs["crc32"] = [](CompilerContext& ctx, const BF& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo(); llvm::Type* i32 = ctx.builder.getInt32Ty(); llvm::Type* i64 = ctx.builder.getInt64Ty();
            auto v = BytesHelp::view(ctx, args, 0);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(ctx.getFunction("aiml_bytes_crc32", i64, {i8p, i32, i64, i64}),
                {v.first, v.second, BytesHelp::i64arg(ctx, args, 1, 0), BytesHelp::i64arg(ctx, args, 2, -1)}));
        };
        // bytes.compare(a, a_off, b, b_off, len) → i32 (-1/0/1, 2 = range outside)
        bns.builtinFuncs["compare"] = [](CompilerContext& ctx, const BF& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo(); llvm::Type* i32 = ctx.builder.getInt32Ty(); llvm::Type* i64 = ctx.builder.getInt64Ty();
            auto a = BytesHelp::view(ctx, args, 0); auto b = BytesHelp::view(ctx, args, 2);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(ctx.getFunction("aiml_bytes_compare", i32, {i8p, i32, i64, i8p, i32, i64, i64}),
                {a.first, a.second, BytesHelp::i64arg(ctx, args, 1, 0), b.first, b.second, BytesHelp::i64arg(ctx, args, 3, 0), BytesHelp::i64arg(ctx, args, 4, 0)}));
        };
        // bytes.delta_apply(base, delta) → buffer<u8> (git pack delta; empty = malformed)
        bns.builtinFuncs["delta_apply"] = [](CompilerContext& ctx, const BF& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo(); llvm::Type* i32 = ctx.builder.getInt32Ty();
            auto b = BytesHelp::view(ctx, args, 0); auto d = BytesHelp::view(ctx, args, 1);
            return BytesHelp::buf(ctx, ctx.builder.CreateCall(ctx.getFunction("aiml_bytes_delta_apply", i8p, {i8p, i32, i8p, i32}),
                {b.first, b.second, d.first, d.second}));
        };

        // ── fsmeta namespace: lstat fields, chmod/chown/mknod, mtimes, xattrs ──
        // (bytes_runtime.cpp; nothing follows symlinks).
        auto& fns = ctx.namespaces["fsmeta"];
        struct FsHelp {
            static llvm::Value* cstr(CompilerContext& ctx, const std::vector<IRValue*>& args, size_t i) {
                if (args.size() > i && args[i]) {
                    llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]));
                    if (v->getType()->isPointerTy()) return v;
                }
                return ctx.builder.CreateGlobalStringPtr("");
            }
        };
        // fsmeta.stat(path, field) → i64
        fns.builtinFuncs["stat"] = [](CompilerContext& ctx, const BF& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo(); llvm::Type* i32 = ctx.builder.getInt32Ty(); llvm::Type* i64 = ctx.builder.getInt64Ty();
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(ctx.getFunction("aiml_fsmeta_stat", i64, {i8p, i32}),
                {FsHelp::cstr(ctx, args, 0), BytesHelp::i32arg(ctx, args, 1, 0)}));
        };
        // fsmeta.chmod(path, mode) → i32
        fns.builtinFuncs["chmod"] = [](CompilerContext& ctx, const BF& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo(); llvm::Type* i32 = ctx.builder.getInt32Ty(); llvm::Type* i64 = ctx.builder.getInt64Ty();
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(ctx.getFunction("aiml_fsmeta_chmod", i32, {i8p, i64}),
                {FsHelp::cstr(ctx, args, 0), BytesHelp::i64arg(ctx, args, 1, 0644)}));
        };
        // fsmeta.chown(path, uid, gid) → i32
        fns.builtinFuncs["chown"] = [](CompilerContext& ctx, const BF& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo(); llvm::Type* i32 = ctx.builder.getInt32Ty(); llvm::Type* i64 = ctx.builder.getInt64Ty();
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(ctx.getFunction("aiml_fsmeta_chown", i32, {i8p, i64, i64}),
                {FsHelp::cstr(ctx, args, 0), BytesHelp::i64arg(ctx, args, 1, -1), BytesHelp::i64arg(ctx, args, 2, -1)}));
        };
        // fsmeta.mknod(path, mode, major, minor) → i32
        fns.builtinFuncs["mknod"] = [](CompilerContext& ctx, const BF& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo(); llvm::Type* i32 = ctx.builder.getInt32Ty(); llvm::Type* i64 = ctx.builder.getInt64Ty();
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(ctx.getFunction("aiml_fsmeta_mknod", i32, {i8p, i64, i64, i64}),
                {FsHelp::cstr(ctx, args, 0), BytesHelp::i64arg(ctx, args, 1, 0), BytesHelp::i64arg(ctx, args, 2, 0), BytesHelp::i64arg(ctx, args, 3, 0)}));
        };
        // fsmeta.set_mtime(path, sec, nsec = 0) → i32
        fns.builtinFuncs["set_mtime"] = [](CompilerContext& ctx, const BF& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo(); llvm::Type* i32 = ctx.builder.getInt32Ty(); llvm::Type* i64 = ctx.builder.getInt64Ty();
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(ctx.getFunction("aiml_fsmeta_set_mtime", i32, {i8p, i64, i64}),
                {FsHelp::cstr(ctx, args, 0), BytesHelp::i64arg(ctx, args, 1, 0), BytesHelp::i64arg(ctx, args, 2, 0)}));
        };
        // fsmeta.xattrs(path) → string (names, one per line)
        fns.builtinFuncs["xattrs"] = [](CompilerContext& ctx, const BF& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            return BytesHelp::str(ctx, ctx.builder.CreateCall(ctx.getFunction("aiml_fsmeta_xattrs", i8p, {i8p}),
                {FsHelp::cstr(ctx, args, 0)}));
        };
        // fsmeta.xattr_get(path, name) → buffer<u8>
        fns.builtinFuncs["xattr_get"] = [](CompilerContext& ctx, const BF& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            return BytesHelp::buf(ctx, ctx.builder.CreateCall(ctx.getFunction("aiml_fsmeta_xattr_get", i8p, {i8p, i8p}),
                {FsHelp::cstr(ctx, args, 0), FsHelp::cstr(ctx, args, 1)}));
        };
        // fsmeta.xattr_set(path, name, value) → i32
        fns.builtinFuncs["xattr_set"] = [](CompilerContext& ctx, const BF& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo(); llvm::Type* i32 = ctx.builder.getInt32Ty();
            auto v = BytesHelp::view(ctx, args, 2);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(ctx.getFunction("aiml_fsmeta_xattr_set", i32, {i8p, i8p, i8p, i32}),
                {FsHelp::cstr(ctx, args, 0), FsHelp::cstr(ctx, args, 1), v.first, v.second}));
        };
    }

    // ── compress namespace ────────────────────────────────────────────────
    // NOTE: Handler lambdas must NOT capture local variables by reference —
    // they are stored and called later after registerBuiltins() has returned.
    // All LLVM types must be recomputed from the ctx parameter inside the lambda.
    {
        auto& compress = ctx.namespaces["compress"];

        /* Helper: memcpy a heap AimlCompressBuffer* into a fresh stack alloca and return it.
         * visitDeclStmt will then memcpy the alloca content into the target buffer<u8> alloca. */
#define COMP_HEAP_TO_ALLOCA(heapPtr) \
        ([&]() -> llvm::Value* { \
            auto* _p   = llvm::PointerType::get(ctx.context, 0); \
            auto* _szt = ctx.getSizeType(); \
            auto* _bt  = ctx.getLLVMType("buffer"); \
            /* memV3: an owned buffer HANDLE (a stack buffer would be "released" by the */ \
            /* local's drop → free() of stack memory), as io.read_bytes / base64.decode. */ \
            llvm::Value* _out = ctx.memV3 \
                ? static_cast<llvm::Value*>(ctx.builder.CreateCall(ctx.getFunction("aiml_buffer_handle_new", _p, {}), {}, "comp_buf_h")) \
                : static_cast<llvm::Value*>(ctx.createEntryAlloca(_bt, "comp_alloca")); \
            uint64_t _sz = ctx.module().getDataLayout().getTypeAllocSize(_bt); \
            llvm::Function* _mcf = ctx.getFunction("memcpy", _p, {_p, _p, _szt}); \
            llvm::Value* _hp = (heapPtr); \
            llvm::Function* _cur = ctx.builder.GetInsertBlock()->getParent(); \
            auto* _copy = llvm::BasicBlock::Create(ctx.context, "comp_copy", _cur); \
            auto* _done = llvm::BasicBlock::Create(ctx.context, "comp_done", _cur); \
            ctx.builder.CreateCondBr(ctx.builder.CreateIsNull(_hp), _done, _copy); \
            ctx.builder.SetInsertPoint(_copy); \
            ctx.builder.CreateCall(_mcf, {_out, _hp, ctx.getSizeConst(_sz)}); \
            ctx.builder.CreateCall(ctx.getFunction("free", ctx.builder.getVoidTy(), {_p}), {_hp}); \
            ctx.builder.CreateBr(_done); \
            ctx.builder.SetInsertPoint(_done); \
            return _out; \
        }())

        /* Helper: store input buffer<u8> struct value into a temp alloca → AimlCompressBuffer* */
#define COMP_IN_ALLOCA(argVal) \
        ([&]() -> llvm::Value* { \
            auto* _bt  = ctx.getLLVMType("buffer"); \
            auto* _tmp = ctx.createEntryAlloca(_bt, "decomp_in"); \
            ctx.builder.CreateStore((argVal), _tmp); \
            return _tmp; \
        }())

        /* compress.X(data, [level]): string/buffer<u8> → buffer<u8>
         * Always passes len=-1 so the C runtime uses strlen for string inputs. */
#define REG_COMP(aimlName, cName) \
        compress.builtinFuncs[aimlName] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* { \
            auto* p   = llvm::PointerType::get(ctx.context, 0); \
            auto* i32 = ctx.builder.getInt32Ty(); \
            llvm::Function* fn = ctx.module().getFunction(cName); \
            if (!fn) fn = llvm::Function::Create( \
                llvm::FunctionType::get(p, {p, i32, i32}, false), \
                llvm::Function::ExternalLinkage, cName, ctx.module()); \
            llvm::Value* data  = args.size() > 0 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])) \
                                                 : llvm::Constant::getNullValue(p); \
            llvm::Value* level = args.size() > 1 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])) \
                                                 : ctx.builder.getInt32(0); \
            llvm::Value* dataLen = ctx.builder.getInt32(-1);   /* -1: a string, up to its NUL */ \
            if (data->getType()->isStructTy()) bufferArgDataLen(ctx, data, data, dataLen);   /* buffer<u8> */ \
            llvm::Value* heapPtr = ctx.builder.CreateCall(fn, {data, dataLen, level}); \
            return ctx.ir_gen->wrap(COMP_HEAP_TO_ALLOCA(heapPtr)); \
        };

        /* compress.X_decompress_str(buf): buffer<u8> → string (char*) */
#define REG_DECOMP_STR(aimlName, cName) \
        compress.builtinFuncs[aimlName] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* { \
            auto* p = llvm::PointerType::get(ctx.context, 0); \
            llvm::Function* fn = ctx.module().getFunction(cName); \
            if (!fn) fn = llvm::Function::Create( \
                llvm::FunctionType::get(p, {p}, false), \
                llvm::Function::ExternalLinkage, cName, ctx.module()); \
            llvm::Value* inPtr = (args.size() > 0) \
                ? COMP_IN_ALLOCA(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))) \
                : llvm::Constant::getNullValue(p); \
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {inPtr})); \
        };

        /* compress.X_decompress(buf): buffer<u8> → buffer<u8> */
#define REG_DECOMP_BUF(aimlName, cName) \
        compress.builtinFuncs[aimlName] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* { \
            auto* p = llvm::PointerType::get(ctx.context, 0); \
            llvm::Function* fn = ctx.module().getFunction(cName); \
            if (!fn) fn = llvm::Function::Create( \
                llvm::FunctionType::get(p, {p}, false), \
                llvm::Function::ExternalLinkage, cName, ctx.module()); \
            llvm::Value* inPtr = (args.size() > 0) \
                ? COMP_IN_ALLOCA(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))) \
                : llvm::Constant::getNullValue(p); \
            llvm::Value* heapPtr = ctx.builder.CreateCall(fn, {inPtr}); \
            return ctx.ir_gen->wrap(COMP_HEAP_TO_ALLOCA(heapPtr)); \
        };

        /* compress.tar_gz / zip: (i8* src, i8* dst) → i32 */
#define REG_ARCHIVE(aimlName, cName) \
        compress.builtinFuncs[aimlName] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* { \
            auto* p   = llvm::PointerType::get(ctx.context, 0); \
            auto* i32 = ctx.builder.getInt32Ty(); \
            llvm::Function* fn = ctx.module().getFunction(cName); \
            if (!fn) fn = llvm::Function::Create( \
                llvm::FunctionType::get(i32, {p, p}, false), \
                llvm::Function::ExternalLinkage, cName, ctx.module()); \
            llvm::Value* a0 = args.size() > 0 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])) \
                                              : llvm::Constant::getNullValue(p); \
            llvm::Value* a1 = args.size() > 1 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])) \
                                              : llvm::Constant::getNullValue(p); \
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {a0, a1})); \
        };

        REG_COMP("gzip",    "aiml_compress_gzip")
        REG_COMP("deflate", "aiml_compress_deflate")
        REG_COMP("zlib",    "aiml_compress_zlib")
        REG_COMP("brotli",  "aiml_compress_brotli")
        REG_COMP("zstd",    "aiml_compress_zstd")

        REG_DECOMP_BUF("gunzip",                "aiml_compress_gunzip")
        REG_DECOMP_STR("gunzip_str",            "aiml_compress_gunzip_str")
        REG_DECOMP_BUF("inflate",               "aiml_compress_inflate")
        REG_DECOMP_STR("inflate_str",           "aiml_compress_inflate_str")
        REG_DECOMP_BUF("zlib_decompress",       "aiml_compress_zlib_decompress")
        REG_DECOMP_STR("zlib_decompress_str",   "aiml_compress_zlib_decompress_str")
        REG_DECOMP_BUF("brotli_decompress",     "aiml_compress_brotli_decompress")
        REG_DECOMP_STR("brotli_decompress_str", "aiml_compress_brotli_decompress_str")
        REG_DECOMP_BUF("zstd_decompress",       "aiml_compress_zstd_decompress")
        REG_DECOMP_STR("zstd_decompress_str",   "aiml_compress_zstd_decompress_str")
        REG_DECOMP_BUF("lz4_decompress",        "aiml_compress_lz4_decompress")
        REG_DECOMP_STR("lz4_decompress_str",    "aiml_compress_lz4_decompress_str")

        /* lz4 compress: (i8* data, i32 len) → buffer<u8>  — no level parameter */
        compress.builtinFuncs["lz4"] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Function* fn = ctx.module().getFunction("aiml_compress_lz4");
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(p, {p, i32}, false),
                llvm::Function::ExternalLinkage, "aiml_compress_lz4", ctx.module());
            llvm::Value* data = args.size() > 0
                ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))
                : llvm::Constant::getNullValue(p);
            llvm::Value* heapPtr = ctx.builder.CreateCall(fn, {data, ctx.builder.getInt32(-1)});
            return ctx.ir_gen->wrap(COMP_HEAP_TO_ALLOCA(heapPtr));
        };

        REG_ARCHIVE("tar_gz",   "aiml_compress_tar_gz")
        REG_ARCHIVE("untar_gz", "aiml_compress_untar_gz")
        REG_ARCHIVE("untar",    "aiml_compress_untar")    // spec/33_2 §3.7a: any tar
        REG_ARCHIVE("unar",     "aiml_compress_unar")     // ar members (.deb)
        REG_ARCHIVE("zip",      "aiml_compress_zip")
        REG_ARCHIVE("unzip",    "aiml_compress_unzip")

        /* compress.zim_open(path) → zim_handle (spec/33_2 §6.1, F33-2-20).
         * Throws ZimError when the file cannot be opened or is not a ZIM file. */
        compress.builtinFuncs["zim_open"] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Value* path = args.empty() ? llvm::ConstantPointerNull::get(p)
                                             : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* h = ctx.builder.CreateCall(
                ctx.getFunction("aiml_zim_open", p, {p}), {path}, "zim_open");
            llvm::Value* code = ctx.builder.CreateCall(
                ctx.getFunction("aiml_zim_last_code", i32, {}), {}, "zim_open_code");
            llvm::Value* bad = ctx.builder.CreateICmpNE(code, ctx.builder.getInt32(0), "zim_open_bad");
            llvm::Value* msg = ctx.builder.CreateCall(
                ctx.getFunction("aiml_zim_last_message", p, {}), {}, "zim_open_msg");
            emitGuardMsgCode(ctx, ctx.builder.CreateZExt(bad, ctx.builder.getInt8Ty()),
                             "zim_open_throw", "zim_open_ok", "ZimError", msg, code);
            return ctx.ir_gen->wrap(h);
        };

        /* compress.zim_cache(clusters) → void (spec/33_2 §6.3, F33-2-26).
         * libzim 8 has no setter; it reads ZIM_CLUSTERCACHE while opening, so this applies to
         * archives opened afterwards, and the unit is a NUMBER OF CLUSTERS. */
        compress.builtinFuncs["zim_cache"] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            llvm::Type* i64 = ctx.builder.getInt64Ty();
            llvm::Value* n = llvm::ConstantInt::get(i64, 16);
            if (!args.empty()) {
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                if (v && v->getType()->isIntegerTy())            n = ctx.builder.CreateIntCast(v, i64, true);
                else if (v && v->getType()->isFloatingPointTy()) n = ctx.builder.CreateFPToSI(v, i64);
            } else {
                LOG_ERROR(ctx.fmtLoc() + "compress.zim_cache expects the number of clusters (spec/33_2 F33-2-26)");
            }
            ctx.builder.CreateCall(ctx.getFunction("aiml_zim_cache", ctx.builder.getVoidTy(), {i64}), {n});
            return ctx.ir_gen->getInt32(0);
        };

        /* compress.zip_entries(path) → list<ptr<zip_entry>> */
        compress.builtinFuncs["zip_entries"] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            llvm::StructType* listTy = ListImpl::getListStructType(ctx);
            llvm::Value* listAlloca  = ctx.memV3
                ? static_cast<llvm::Value*>(ctx.builder.CreateCall(ctx.getFunction("aiml_list_handle_new", p, {}), {}, "zip_entries_h"))
                : ctx.createEntryAlloca(listTy, "zip_entries_list");
            ListImpl::initList(ctx, listAlloca, p, 8);
            llvm::Value* pathStr = args.empty() ? llvm::Constant::getNullValue(p)
                                                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Function* fn = ctx.getFunction("aiml_compress_zip_entries_fill",
                ctx.builder.getVoidTy(), {p, p});
            ctx.builder.CreateCall(fn, {pathStr, listAlloca});
            return ctx.ir_gen->wrap(listAlloca);
        };

        /* compress.tar_entries(path) → list<ptr<tar_entry>> */
        compress.builtinFuncs["tar_entries"] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            llvm::StructType* listTy = ListImpl::getListStructType(ctx);
            llvm::Value* listAlloca  = ctx.memV3
                ? static_cast<llvm::Value*>(ctx.builder.CreateCall(ctx.getFunction("aiml_list_handle_new", p, {}), {}, "tar_entries_h"))
                : ctx.createEntryAlloca(listTy, "tar_entries_list");
            ListImpl::initList(ctx, listAlloca, p, 8);
            llvm::Value* pathStr = args.empty() ? llvm::Constant::getNullValue(p)
                                                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Function* fn = ctx.getFunction("aiml_compress_tar_entries_fill",
                ctx.builder.getVoidTy(), {p, p});
            ctx.builder.CreateCall(fn, {pathStr, listAlloca});
            return ctx.ir_gen->wrap(listAlloca);
        };

#undef REG_COMP
#undef REG_DECOMP_STR
#undef REG_DECOMP_BUF
#undef REG_ARCHIVE
#undef COMP_HEAP_TO_ALLOCA
#undef COMP_IN_ALLOCA
    } // end compress namespace

    // =======================================================================
    // ai — native ML acceleration (Spec 51)
    // =======================================================================
    {
        auto& ai = ctx.namespaces["ai"];

        // Helpers: extract struct fields from either a loaded struct (extractvalue)
        // or a pointer to a struct (GEP + load). Args can arrive as either depending
        // on whether they come from local variables (struct) or model/class fields (ptr).
        auto getBufferData = [](CompilerContext& ctx, llvm::Value* val) -> llvm::Value* {
            if (val->getType()->isAggregateType()) {
                return ctx.builder.CreateExtractValue(val, {4}, "buf_data");
            }
            // Pointer to buffer struct — GEP field 4 + load
            auto* bufTy = static_cast<llvm::StructType*>(ctx.getLLVMType("buffer"));
            auto* gep = ctx.builder.CreateStructGEP(bufTy, val, 4, "buf_data_gep");
            return ctx.builder.CreateLoad(ctx.builder.getInt8Ty()->getPointerTo(), gep, "buf_data");
        };
        auto getListField = [](CompilerContext& ctx, llvm::Value* val, unsigned idx,
                               const char* name) -> llvm::Value* {
            if (val->getType()->isAggregateType()) {
                return ctx.builder.CreateExtractValue(val, {idx}, name);
            }
            // Pointer to list struct — GEP + load
            auto* listTy = ListImpl::getListStructType(ctx);
            auto* gep = ctx.builder.CreateStructGEP(listTy, val, idx, (std::string(name) + "_gep").c_str());
            llvm::Type* fieldTy = listTy->getElementType(idx);
            return ctx.builder.CreateLoad(fieldTy, gep, name);
        };

        // ai.embedding_lookup(buffer_var, token_id, dim) → list<f32>
        ai.builtinFuncs["embedding_lookup"] = [getBufferData](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            if (args.size() < 3) return nullptr;
            llvm::Type* i8p   = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32ty = ctx.builder.getInt32Ty();
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);

            llvm::Value* bufVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* flatPtr = getBufferData(ctx, bufVal);

            llvm::Value* tokenId = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            llvm::Value* dim     = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]));

            // Allocate result list<f32>: {size=dim, capacity=dim, data=malloc(dim*4)}
            llvm::StructType* listTy = ListImpl::getListStructType(ctx);
            llvm::Value* listAlloca = ctx.memV3
                ? static_cast<llvm::Value*>(ctx.builder.CreateCall(ctx.getFunction("aiml_list_handle_new", ptrTy, {}), {}, "emb_lookup_h"))
                : ctx.createEntryAlloca(listTy, "emb_lookup_list");
            ctx.builder.CreateStore(dim, ctx.builder.CreateStructGEP(listTy, listAlloca, 0));
            ctx.builder.CreateStore(dim, ctx.builder.CreateStructGEP(listTy, listAlloca, 1));
            llvm::Value* totalBytes = ctx.builder.CreateMul(
                ctx.toSizeType(dim), ctx.getSizeConst(4));
            llvm::Value* dataPtr = ctx.builder.CreateCall(
                ctx.getFunction("malloc", ptrTy, {ctx.getSizeType()}), {totalBytes}, "emb_data");
            ctx.builder.CreateStore(dataPtr, ctx.builder.CreateStructGEP(listTy, listAlloca, 2));

            llvm::Function* lookupFn = ctx.getFunction("aiml_ai_embedding_lookup",
                ctx.builder.getVoidTy(), {i8p, i32ty, i32ty, ptrTy});
            ctx.builder.CreateCall(lookupFn, {flatPtr, tokenId, dim, dataPtr});

            if (ctx.memV3) return ctx.ir_gen->wrap(listAlloca);
            llvm::Value* listVal = ctx.builder.CreateLoad(listTy, listAlloca, "emb_lookup_result");
            return ctx.ir_gen->wrap(listVal);
        };

        // ai.embedding_update(buffer_var, token_id, gradient_list, lr, dim)
        ai.builtinFuncs["embedding_update"] = [getBufferData, getListField](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            if (args.size() < 5) return nullptr;
            llvm::Type* i8p   = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32ty = ctx.builder.getInt32Ty();
            llvm::Type* f32ty = ctx.builder.getFloatTy();
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);

            llvm::Value* bufVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* flatPtr = getBufferData(ctx, bufVal);

            llvm::Value* tokenId = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));

            llvm::Value* gradVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]));
            llvm::Value* gradPtr = getListField(ctx, gradVal, 2, "grad_data");

            llvm::Value* lr  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[3]));
            llvm::Value* dim = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[4]));

            if (lr->getType()->isDoubleTy())
                lr = ctx.builder.CreateFPTrunc(lr, f32ty);
            else if (lr->getType()->isIntegerTy())
                lr = ctx.builder.CreateSIToFP(lr, f32ty);

            llvm::Function* updateFn = ctx.getFunction("aiml_ai_embedding_update",
                ctx.builder.getVoidTy(), {i8p, i32ty, ptrTy, f32ty, i32ty});
            ctx.builder.CreateCall(updateFn, {flatPtr, tokenId, gradPtr, lr, dim});
            return nullptr;
        };

        // ai.dot(list_a, list_b, dim) → f32
        ai.builtinFuncs["dot"] = [getListField](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            if (args.size() < 3) return nullptr;
            llvm::Type* f32ty = ctx.builder.getFloatTy();
            llvm::Type* i32ty = ctx.builder.getInt32Ty();
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);

            llvm::Value* aVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* aPtr = getListField(ctx, aVal, 2, "dot_a");

            llvm::Value* bVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            llvm::Value* bPtr = getListField(ctx, bVal, 2, "dot_b");

            llvm::Value* dim = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]));

            llvm::Function* dotFn = ctx.getFunction("aiml_ai_dot", f32ty, {ptrTy, ptrTy, i32ty});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(dotFn, {aPtr, bPtr, dim}, "dot_result"));
        };

        // ai.train_bpe_merges(texts_list, max_merges) → opaque ptr (AimlBpeMergeResult*)
        ai.builtinFuncs["train_bpe_merges"] = [getListField](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            if (args.size() < 2) return nullptr;
            llvm::Type* i32ty = ctx.builder.getInt32Ty();
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);

            llvm::Value* listVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* nTexts  = getListField(ctx, listVal, 0, "n_texts");
            llvm::Value* textsPtr = getListField(ctx, listVal, 2, "texts_ptr");

            llvm::Value* maxMerges = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));

            llvm::Function* trainFn = ctx.getFunction("aiml_bpe_train_merges",
                ptrTy, {ptrTy, i32ty, i32ty});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(trainFn, {textsPtr, nTexts, maxMerges}, "bpe_result"));
        };

        // ai.bpe_merge_count(result_ptr) → i32
        ai.builtinFuncs["bpe_merge_count"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            if (args.empty()) return nullptr;
            llvm::Type* i32ty = ctx.builder.getInt32Ty();
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* ptr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::StructType* resTy = llvm::StructType::get(ctx.context, {ptrTy, ptrTy, i32ty});
            llvm::Value* countGep = ctx.builder.CreateStructGEP(resTy, ptr, 2, "merge_count");
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(i32ty, countGep));
        };

        // ai.bpe_merge_left(result_ptr, index) → string
        ai.builtinFuncs["bpe_merge_left"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            if (args.size() < 2) return nullptr;
            llvm::Type* i32ty = ctx.builder.getInt32Ty();
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* ptr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* idx = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            llvm::StructType* resTy = llvm::StructType::get(ctx.context, {ptrTy, ptrTy, i32ty});
            llvm::Value* leftArrGep = ctx.builder.CreateStructGEP(resTy, ptr, 0);
            llvm::Value* leftArr = ctx.builder.CreateLoad(ptrTy, leftArrGep);
            llvm::Value* elemGep = ctx.builder.CreateGEP(ptrTy, leftArr, {idx}, "left_elem");
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(ptrTy, elemGep, "left_str"));
        };

        // ai.bpe_merge_right(result_ptr, index) → string
        ai.builtinFuncs["bpe_merge_right"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            if (args.size() < 2) return nullptr;
            llvm::Type* i32ty = ctx.builder.getInt32Ty();
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* ptr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* idx = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            llvm::StructType* resTy = llvm::StructType::get(ctx.context, {ptrTy, ptrTy, i32ty});
            llvm::Value* rightArrGep = ctx.builder.CreateStructGEP(resTy, ptr, 1);
            llvm::Value* rightArr = ctx.builder.CreateLoad(ptrTy, rightArrGep);
            llvm::Value* elemGep = ctx.builder.CreateGEP(ptrTy, rightArr, {idx}, "right_elem");
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(ptrTy, elemGep, "right_str"));
        };

        // ai.bpe_merge_free(result_ptr)
        ai.builtinFuncs["bpe_merge_free"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            if (args.empty()) return nullptr;
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* ptr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Function* freeFn = ctx.getFunction("aiml_bpe_merge_result_free",
                ctx.builder.getVoidTy(), {ptrTy});
            ctx.builder.CreateCall(freeFn, {ptr});
            return nullptr;
        };

        // ai.encode_dense(token_ids_list, buffer_var, dim) → list<f32>
        ai.builtinFuncs["encode_dense"] = [getBufferData, getListField](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            if (args.size() < 3) return nullptr;
            llvm::Type* i32ty = ctx.builder.getInt32Ty();
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
            llvm::Type* i8p   = ctx.builder.getInt8Ty()->getPointerTo();

            llvm::Value* idsVal  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* nTokens = getListField(ctx, idsVal, 0, "enc_n");
            llvm::Value* idsPtr  = getListField(ctx, idsVal, 2, "enc_ids");

            llvm::Value* bufVal  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            llvm::Value* flatPtr = getBufferData(ctx, bufVal);

            llvm::Value* dim = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]));

            llvm::StructType* listTy = ListImpl::getListStructType(ctx);
            // spec/61 §5: a list-returning builtin must hand back a HEAP HANDLE under memV3 (a value
            // local/global adopts the returned rc), not an inline struct — else the adopt strides the
            // struct bytes as a handle pointer → crash (test_ai_dense `enc.len` on a global list<f32>).
            llvm::Value* listAlloca = ctx.memV3
                ? static_cast<llvm::Value*>(ctx.builder.CreateCall(ctx.getFunction("aiml_list_handle_new", i8p, {}), {}, "enc_dense_h"))
                : ctx.createEntryAlloca(listTy, "enc_dense_list");
            ctx.builder.CreateStore(dim, ctx.builder.CreateStructGEP(listTy, listAlloca, 0));
            ctx.builder.CreateStore(dim, ctx.builder.CreateStructGEP(listTy, listAlloca, 1));
            llvm::Value* totalBytes = ctx.builder.CreateMul(
                ctx.toSizeType(dim), ctx.getSizeConst(4));
            llvm::Value* dataPtr = ctx.builder.CreateCall(
                ctx.getFunction("malloc", ptrTy, {ctx.getSizeType()}), {totalBytes}, "enc_data");
            ctx.builder.CreateStore(dataPtr, ctx.builder.CreateStructGEP(listTy, listAlloca, 2));

            llvm::Function* fn = ctx.getFunction("aiml_ai_encode_dense",
                ctx.builder.getVoidTy(), {ptrTy, i32ty, i8p, i32ty, ptrTy});
            ctx.builder.CreateCall(fn, {idsPtr, nTokens, flatPtr, dim, dataPtr});

            if (ctx.memV3) return ctx.ir_gen->wrap(listAlloca);   // handle pointer (adopted by caller)
            llvm::Value* listVal = ctx.builder.CreateLoad(listTy, listAlloca, "enc_dense_result");
            return ctx.ir_gen->wrap(listVal);
        };

        // ai.encode_dense_weighted(token_ids_list, buffer_var, dim) → list<f32>
        ai.builtinFuncs["encode_dense_weighted"] = [getBufferData, getListField](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            if (args.size() < 3) return nullptr;
            llvm::Type* i32ty = ctx.builder.getInt32Ty();
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
            llvm::Type* i8p   = ctx.builder.getInt8Ty()->getPointerTo();

            llvm::Value* idsVal  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* nTokens = getListField(ctx, idsVal, 0, "encw_n");
            llvm::Value* idsPtr  = getListField(ctx, idsVal, 2, "encw_ids");

            llvm::Value* bufVal  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            llvm::Value* flatPtr = getBufferData(ctx, bufVal);

            llvm::Value* dim = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]));

            llvm::StructType* listTy = ListImpl::getListStructType(ctx);
            // §5: heap handle under memV3 (see encode_dense) so the caller adopts an rc, not a struct.
            llvm::Value* listAlloca = ctx.memV3
                ? static_cast<llvm::Value*>(ctx.builder.CreateCall(ctx.getFunction("aiml_list_handle_new", i8p, {}), {}, "encw_h"))
                : ctx.createEntryAlloca(listTy, "encw_list");
            ctx.builder.CreateStore(dim, ctx.builder.CreateStructGEP(listTy, listAlloca, 0));
            ctx.builder.CreateStore(dim, ctx.builder.CreateStructGEP(listTy, listAlloca, 1));
            llvm::Value* totalBytes = ctx.builder.CreateMul(
                ctx.toSizeType(dim), ctx.getSizeConst(4));
            llvm::Value* dataPtr = ctx.builder.CreateCall(
                ctx.getFunction("malloc", ptrTy, {ctx.getSizeType()}), {totalBytes}, "encw_data");
            ctx.builder.CreateStore(dataPtr, ctx.builder.CreateStructGEP(listTy, listAlloca, 2));

            llvm::Function* fn = ctx.getFunction("aiml_ai_encode_dense_weighted",
                ctx.builder.getVoidTy(), {ptrTy, i32ty, i8p, i32ty, ptrTy});
            ctx.builder.CreateCall(fn, {idsPtr, nTokens, flatPtr, dim, dataPtr});

            if (ctx.memV3) return ctx.ir_gen->wrap(listAlloca);   // handle pointer
            llvm::Value* listVal = ctx.builder.CreateLoad(listTy, listAlloca, "encw_result");
            return ctx.ir_gen->wrap(listVal);
        };

        // ai.triplet_loss(q_list, pos_list, neg_list, margin, dim) → f32
        ai.builtinFuncs["triplet_loss"] = [getListField](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            if (args.size() < 5) return nullptr;
            llvm::Type* f32ty = ctx.builder.getFloatTy();
            llvm::Type* i32ty = ctx.builder.getInt32Ty();
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);

            llvm::Value* qVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* qPtr = getListField(ctx, qVal, 2, "tl_q");

            llvm::Value* posVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            llvm::Value* posPtr = getListField(ctx, posVal, 2, "tl_pos");

            llvm::Value* negVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]));
            llvm::Value* negPtr = getListField(ctx, negVal, 2, "tl_neg");

            llvm::Value* margin = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[3]));
            if (margin->getType()->isDoubleTy())
                margin = ctx.builder.CreateFPTrunc(margin, f32ty);
            else if (margin->getType()->isIntegerTy())
                margin = ctx.builder.CreateSIToFP(margin, f32ty);

            llvm::Value* dim = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[4]));

            llvm::Function* fn = ctx.getFunction("aiml_ai_triplet_loss",
                f32ty, {ptrTy, ptrTy, ptrTy, f32ty, i32ty});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {qPtr, posPtr, negPtr, margin, dim}, "triplet_loss"));
        };

        // ai.triplet_grad(q_list, pos_list, neg_list, margin, dim) → list<f32>
        ai.builtinFuncs["triplet_grad"] = [getListField](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            if (args.size() < 5) return nullptr;
            llvm::Type* f32ty = ctx.builder.getFloatTy();
            llvm::Type* i32ty = ctx.builder.getInt32Ty();
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);

            llvm::Value* qVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* qPtr = getListField(ctx, qVal, 2, "tg_q");
            llvm::Value* qDim = getListField(ctx, qVal, 0, "tg_dim");

            llvm::Value* posVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            llvm::Value* posPtr = getListField(ctx, posVal, 2, "tg_pos");

            llvm::Value* negVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]));
            llvm::Value* negPtr = getListField(ctx, negVal, 2, "tg_neg");

            llvm::Value* margin = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[3]));
            if (margin->getType()->isDoubleTy())
                margin = ctx.builder.CreateFPTrunc(margin, f32ty);
            else if (margin->getType()->isIntegerTy())
                margin = ctx.builder.CreateSIToFP(margin, f32ty);

            llvm::Value* dim = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[4]));

            llvm::StructType* listTy = ListImpl::getListStructType(ctx);
            // §5: heap handle under memV3 (see encode_dense) so the caller adopts an rc, not a struct.
            llvm::Value* listAlloca = ctx.memV3
                ? static_cast<llvm::Value*>(ctx.builder.CreateCall(ctx.getFunction("aiml_list_handle_new", ptrTy, {}), {}, "tg_h"))
                : ctx.createEntryAlloca(listTy, "tg_list");
            ctx.builder.CreateStore(dim, ctx.builder.CreateStructGEP(listTy, listAlloca, 0));
            ctx.builder.CreateStore(dim, ctx.builder.CreateStructGEP(listTy, listAlloca, 1));
            llvm::Value* totalBytes = ctx.builder.CreateMul(
                ctx.toSizeType(dim), ctx.getSizeConst(4));
            llvm::Value* dataPtr = ctx.builder.CreateCall(
                ctx.getFunction("malloc", ptrTy, {ctx.getSizeType()}), {totalBytes}, "tg_data");
            ctx.builder.CreateStore(dataPtr, ctx.builder.CreateStructGEP(listTy, listAlloca, 2));

            llvm::Function* fn = ctx.getFunction("aiml_ai_triplet_grad",
                ctx.builder.getVoidTy(), {ptrTy, ptrTy, ptrTy, f32ty, i32ty, ptrTy});
            ctx.builder.CreateCall(fn, {qPtr, posPtr, negPtr, margin, dim, dataPtr});

            if (ctx.memV3) return ctx.ir_gen->wrap(listAlloca);   // handle pointer
            llvm::Value* listVal = ctx.builder.CreateLoad(listTy, listAlloca, "tg_result");
            return ctx.ir_gen->wrap(listVal);
        };

        // ai.sample_logits(tensor, temperature, top_k) → i32
        ai.builtinFuncs["sample_logits"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            if (args.size() < 3) return nullptr;
            llvm::Type* i32ty = ctx.builder.getInt32Ty();
            llvm::Type* f32ty = ctx.builder.getFloatTy();
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);

            llvm::Value* tensorPtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));

            llvm::Value* temperature = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            if (temperature->getType()->isDoubleTy())
                temperature = ctx.builder.CreateFPTrunc(temperature, f32ty);
            else if (temperature->getType()->isIntegerTy())
                temperature = ctx.builder.CreateSIToFP(temperature, f32ty);

            llvm::Value* topK = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]));

            llvm::Function* fn = ctx.getFunction("aiml_ai_sample_logits",
                i32ty, {ptrTy, f32ty, i32ty});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {tensorPtr, temperature, topK}, "sampled_id"));
        };

    } // end ai namespace

    // =======================================================================
    // gltf — 3D asset loader (cgltf + stb_image backend)
    // All handle types are opaque i8* pointers with preLoadBase = true.
    // =======================================================================
    {
        // ── Helper: memcpy a heap AimlGltfBuffer* into a stack alloca ──────
// spec/61 §5 (memV3): the runtime's heap AimlGltfBuffer becomes an owned buffer handle (struct
// moved into the handle, header freed); the data block belongs to the handle.
#define GLTF_HEAP_TO_ALLOCA(heapPtr) \
        ([&]() -> llvm::Value* { \
            auto* _p  = llvm::PointerType::get(ctx.context, 0); \
            auto* _szt= ctx.getSizeType(); \
            auto* _bt = ctx.getLLVMType("buffer"); \
            uint64_t _sz = ctx.module().getDataLayout().getTypeAllocSize(_bt); \
            llvm::Value* _hp = (heapPtr); \
            llvm::Value* _out = ctx.memV3 \
                ? static_cast<llvm::Value*>(ctx.builder.CreateCall(ctx.getFunction("aiml_buffer_handle_new", _p, {}), {}, "gltf_buf_h")) \
                : static_cast<llvm::Value*>(ctx.createEntryAlloca(_bt, "gltf_buf")); \
            ctx.builder.CreateCall(ctx.getFunction("memcpy", _p, {_p, _p, _szt}), \
                {_out, _hp, ctx.getSizeConst(_sz)}); \
            if (ctx.memV3) \
                ctx.builder.CreateCall(ctx.getFunction("free", ctx.builder.getVoidTy(), {_p}), {_hp}); \
            return _out; \
        }())

        // ── Simple method macros ────────────────────────────────────────────
        // ci: ClassInfo&, aimlName: string literal, cName: C symbol string literal
// spec/43_1 + spec/61 §7: the runtime hands out cgltf-owned char*; the result is an owned rc copy
// (statement temp), valid after scene.free.
#define GLTF_STR(ci, aimlName, cName) \
        (ci).builtinMethods[aimlName] = [](CompilerContext& ctx, llvm::Value* base, \
            AIML::AST::CallExpr*, auto) -> IRValue* { \
            auto* p = llvm::PointerType::get(ctx.context, 0); \
            llvm::Value* raw = ctx.builder.CreateCall(ctx.getFunction(cName, p, {p}), {base}); \
            llvm::Value* owned = ctx.builder.CreateCall(ctx.getFunction("aiml_str_dup", p, {p}), {raw}, "gltf_str"); \
            if (ctx.lifetimeDropsEnabled && !ctx.stmtTempStack.empty()) \
                AIML::AST::aimlTrackStringTemp(ctx, owned); \
            return ctx.ir_gen->wrap(owned); \
        };

#define GLTF_I32M(ci, aimlName, cName) \
        (ci).builtinMethods[aimlName] = [](CompilerContext& ctx, llvm::Value* base, \
            AIML::AST::CallExpr*, auto) -> IRValue* { \
            auto* p   = llvm::PointerType::get(ctx.context, 0); \
            auto* i32 = ctx.builder.getInt32Ty(); \
            return ctx.ir_gen->wrap(ctx.builder.CreateCall( \
                ctx.getFunction(cName, i32, {p}), {base})); \
        };

#define GLTF_F32M(ci, aimlName, cName) \
        (ci).builtinMethods[aimlName] = [](CompilerContext& ctx, llvm::Value* base, \
            AIML::AST::CallExpr*, auto) -> IRValue* { \
            auto* p   = llvm::PointerType::get(ctx.context, 0); \
            auto* f32 = ctx.builder.getFloatTy(); \
            return ctx.ir_gen->wrap(ctx.builder.CreateCall( \
                ctx.getFunction(cName, f32, {p}), {base})); \
        };

#define GLTF_PTRM(ci, aimlName, cName) \
        (ci).builtinMethods[aimlName] = [](CompilerContext& ctx, llvm::Value* base, \
            AIML::AST::CallExpr*, auto) -> IRValue* { \
            auto* p = llvm::PointerType::get(ctx.context, 0); \
            return ctx.ir_gen->wrap(ctx.builder.CreateCall( \
                ctx.getFunction(cName, p, {p}), {base})); \
        };

        // Buffer-returning method: C fn returns heap AimlGltfBuffer* → memcpy to alloca
#define GLTF_BUFM(ci, aimlName, cName) \
        (ci).builtinMethods[aimlName] = [](CompilerContext& ctx, llvm::Value* base, \
            AIML::AST::CallExpr*, auto) -> IRValue* { \
            auto* p = llvm::PointerType::get(ctx.context, 0); \
            llvm::Value* hp = ctx.builder.CreateCall( \
                ctx.getFunction(cName, p, {p}), {base}); \
            return ctx.ir_gen->wrap(GLTF_HEAP_TO_ALLOCA(hp)); \
        };

        // List-fill method: C fn takes (scene/node/skin, list*) → void
#define GLTF_LIST(ci, aimlName, cName) \
        (ci).builtinMethods[aimlName] = [](CompilerContext& ctx, llvm::Value* base, \
            AIML::AST::CallExpr*, auto) -> IRValue* { \
            auto* p  = llvm::PointerType::get(ctx.context, 0); \
            auto* vt = ctx.builder.getVoidTy(); \
            llvm::StructType* lt = ListImpl::getListStructType(ctx); \
            llvm::Value* la = ctx.memV3 \
                ? static_cast<llvm::Value*>(ctx.builder.CreateCall(ctx.getFunction("aiml_list_handle_new", p, {}), {}, "gltf_list_h")) \
                : static_cast<llvm::Value*>(ctx.createEntryAlloca(lt, "gltf_list")); \
            ListImpl::initList(ctx, la, p, 8); \
            ctx.builder.CreateCall(ctx.getFunction(cName, vt, {p, p}), {base, la}); \
            return ctx.ir_gen->wrap(la); \
        };

        // ── gltf_scene ──────────────────────────────────────────────────────
        {
            CompilerContext::ClassInfo ci;
            ci.name        = "gltf_scene";
            ci.isBuiltin   = true;
            ci.preLoadBase = true;

            GLTF_I32M(ci, "valid",  "aiml_gltf_scene_valid")
            GLTF_STR (ci, "error",  "aiml_gltf_scene_error")
            GLTF_LIST(ci, "nodes",       "aiml_gltf_scene_nodes_fill")
            GLTF_LIST(ci, "meshes",      "aiml_gltf_scene_meshes_fill")
            GLTF_LIST(ci, "materials",   "aiml_gltf_scene_materials_fill")
            GLTF_LIST(ci, "animations",  "aiml_gltf_scene_animations_fill")
            GLTF_LIST(ci, "skins",       "aiml_gltf_scene_skins_fill")
            GLTF_LIST(ci, "images",      "aiml_gltf_scene_images_fill")
            GLTF_LIST(ci, "extensions_used",     "aiml_gltf_scene_extensions_used_fill")
            GLTF_LIST(ci, "extensions_required", "aiml_gltf_scene_extensions_required_fill")
            GLTF_STR (ci, "extras",      "aiml_gltf_scene_extras")
            GLTF_I32M(ci, "node_count",  "aiml_gltf_scene_node_count")
            GLTF_I32M(ci, "image_count", "aiml_gltf_scene_image_count")

            // scene.image_at(index: i32) → owned gltf_image (spec/61 Class 4), null out of range
            ci.builtinMethods["image_at"] = [](CompilerContext& ctx, llvm::Value* base,
                AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                auto* p   = llvm::PointerType::get(ctx.context, 0);
                auto* i32 = ctx.builder.getInt32Ty();
                llvm::Value* idx = ctx.builder.getInt32(0);
                if (call && !call->args.empty()) {
                    IRValue* a = evalExpr(call->args[0].get());
                    if (a) idx = ctx.builder.CreateSExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a)), i32);
                }
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction("aiml_gltf_scene_image_at", p, {p, i32}), {base, idx}));
            };

            // scene.node_at(index: i32) → gltf_node (null when out of range), spec/43_1
            ci.builtinMethods["node_at"] = [](CompilerContext& ctx, llvm::Value* base,
                AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                auto* p   = llvm::PointerType::get(ctx.context, 0);
                auto* i32 = ctx.builder.getInt32Ty();
                llvm::Value* idx = ctx.builder.getInt32(0);
                if (call && !call->args.empty()) {
                    IRValue* a = evalExpr(call->args[0].get());
                    if (a) idx = ctx.builder.CreateSExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a)), i32);
                }
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction("aiml_gltf_scene_node_at", p, {p, i32}), {base, idx}));
            };

            ci.builtinMethods["free"] = [](CompilerContext& ctx, llvm::Value* base,
                AIML::AST::CallExpr*, auto) -> IRValue* {
                auto* p  = llvm::PointerType::get(ctx.context, 0);
                ctx.builder.CreateCall(ctx.getFunction("aiml_gltf_scene_free",
                    ctx.builder.getVoidTy(), {p}), {base});
                return nullptr;
            };

            ctx.classes["gltf_scene"] = std::move(ci);
        }

        // ── gltf_node ───────────────────────────────────────────────────────
        {
            CompilerContext::ClassInfo ci;
            ci.name        = "gltf_node";
            ci.isBuiltin   = true;
            ci.preLoadBase = true;

            GLTF_STR (ci, "name",          "aiml_gltf_node_name")
            GLTF_F32M(ci, "translation_x", "aiml_gltf_node_translation_x")
            GLTF_F32M(ci, "translation_y", "aiml_gltf_node_translation_y")
            GLTF_F32M(ci, "translation_z", "aiml_gltf_node_translation_z")
            GLTF_F32M(ci, "rotation_x",    "aiml_gltf_node_rotation_x")
            GLTF_F32M(ci, "rotation_y",    "aiml_gltf_node_rotation_y")
            GLTF_F32M(ci, "rotation_z",    "aiml_gltf_node_rotation_z")
            GLTF_F32M(ci, "rotation_w",    "aiml_gltf_node_rotation_w")
            GLTF_F32M(ci, "scale_x",       "aiml_gltf_node_scale_x")
            GLTF_F32M(ci, "scale_y",       "aiml_gltf_node_scale_y")
            GLTF_F32M(ci, "scale_z",       "aiml_gltf_node_scale_z")
            GLTF_PTRM(ci, "mesh",          "aiml_gltf_node_mesh")
            GLTF_PTRM(ci, "skin",          "aiml_gltf_node_skin")
            GLTF_LIST(ci, "children",      "aiml_gltf_node_children_fill")
            GLTF_BUFM(ci, "world_matrix",  "aiml_gltf_node_world_matrix")
            GLTF_BUFM(ci, "local_matrix",  "aiml_gltf_node_local_matrix")

            ctx.classes["gltf_node"] = std::move(ci);
        }

        // ── gltf_mesh ───────────────────────────────────────────────────────
        {
            CompilerContext::ClassInfo ci;
            ci.name        = "gltf_mesh";
            ci.isBuiltin   = true;
            ci.preLoadBase = true;

            GLTF_STR (ci, "name",            "aiml_gltf_mesh_name")
            GLTF_I32M(ci, "primitive_count", "aiml_gltf_mesh_primitive_count")

            ci.builtinMethods["primitive"] = [](CompilerContext& ctx, llvm::Value* base,
                AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                auto* p   = llvm::PointerType::get(ctx.context, 0);
                auto* i32 = ctx.builder.getInt32Ty();
                llvm::Value* idx = ctx.builder.getInt32(0);
                if (call && !call->args.empty()) {
                    IRValue* a = evalExpr(call->args[0].get());
                    if (a) idx = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a));
                }
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction("aiml_gltf_mesh_primitive", p, {p, i32}), {base, idx}));
            };

            ctx.classes["gltf_mesh"] = std::move(ci);
        }

        // ── gltf_primitive ──────────────────────────────────────────────────
        {
            CompilerContext::ClassInfo ci;
            ci.name        = "gltf_primitive";
            ci.isBuiltin   = true;
            ci.preLoadBase = true;

            GLTF_BUFM(ci, "positions", "aiml_gltf_prim_positions")
            GLTF_BUFM(ci, "normals",   "aiml_gltf_prim_normals")
            GLTF_BUFM(ci, "tangents",  "aiml_gltf_prim_tangents")
            GLTF_BUFM(ci, "indices",   "aiml_gltf_prim_indices")
            GLTF_I32M(ci, "vertex_count", "aiml_gltf_prim_vertex_count")
            GLTF_I32M(ci, "index_count",  "aiml_gltf_prim_index_count")
            GLTF_PTRM(ci, "material",     "aiml_gltf_prim_material")
            GLTF_PTRM(ci, "to_gl_mesh",   "aiml_gltf_prim_to_gl_mesh")

            // uvs(set), colors(set), joints(set), weights(set): buffer return + 1 i32 arg
#define GLTF_BUF1(ci, aimlName, cName) \
            (ci).builtinMethods[aimlName] = [](CompilerContext& ctx, llvm::Value* base, \
                AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* { \
                auto* p   = llvm::PointerType::get(ctx.context, 0); \
                auto* i32 = ctx.builder.getInt32Ty(); \
                llvm::Value* set = ctx.builder.getInt32(0); \
                if (call && !call->args.empty()) { \
                    IRValue* a = evalExpr(call->args[0].get()); \
                    if (a) set = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a)); \
                } \
                llvm::Value* hp = ctx.builder.CreateCall( \
                    ctx.getFunction(cName, p, {p, i32}), {base, set}); \
                return ctx.ir_gen->wrap(GLTF_HEAP_TO_ALLOCA(hp)); \
            };

            GLTF_BUF1(ci, "uvs",     "aiml_gltf_prim_uvs")
            GLTF_BUF1(ci, "colors",  "aiml_gltf_prim_colors")
            GLTF_BUF1(ci, "joints",  "aiml_gltf_prim_joints")
            GLTF_BUF1(ci, "weights", "aiml_gltf_prim_weights")
#undef GLTF_BUF1

            ctx.classes["gltf_primitive"] = std::move(ci);
        }

        // ── gltf_material ───────────────────────────────────────────────────
        {
            CompilerContext::ClassInfo ci;
            ci.name        = "gltf_material";
            ci.isBuiltin   = true;
            ci.preLoadBase = true;

            GLTF_STR (ci, "name",          "aiml_gltf_mat_name")
            GLTF_STR (ci, "extras",        "aiml_gltf_mat_extras")
            GLTF_F32M(ci, "base_color_r",  "aiml_gltf_mat_base_color_r")
            GLTF_F32M(ci, "base_color_g",  "aiml_gltf_mat_base_color_g")
            GLTF_F32M(ci, "base_color_b",  "aiml_gltf_mat_base_color_b")
            GLTF_F32M(ci, "base_color_a",  "aiml_gltf_mat_base_color_a")
            GLTF_F32M(ci, "metallic",      "aiml_gltf_mat_metallic")
            GLTF_F32M(ci, "roughness",     "aiml_gltf_mat_roughness")
            GLTF_F32M(ci, "emissive_r",    "aiml_gltf_mat_emissive_r")
            GLTF_F32M(ci, "emissive_g",    "aiml_gltf_mat_emissive_g")
            GLTF_F32M(ci, "emissive_b",    "aiml_gltf_mat_emissive_b")
            GLTF_PTRM(ci, "base_color_texture",          "aiml_gltf_mat_base_color_texture")
            GLTF_PTRM(ci, "normal_texture",               "aiml_gltf_mat_normal_texture")
            GLTF_PTRM(ci, "metallic_roughness_texture",   "aiml_gltf_mat_metallic_roughness_texture")
            GLTF_PTRM(ci, "emissive_texture",             "aiml_gltf_mat_emissive_texture")
            GLTF_PTRM(ci, "occlusion_texture",            "aiml_gltf_mat_occlusion_texture")
            GLTF_STR (ci, "alpha_mode",    "aiml_gltf_mat_alpha_mode")
            GLTF_F32M(ci, "alpha_cutoff",  "aiml_gltf_mat_alpha_cutoff")
            GLTF_I32M(ci, "double_sided",  "aiml_gltf_mat_double_sided")
            GLTF_I32M(ci, "index",              "aiml_gltf_mat_index")
            GLTF_F32M(ci, "normal_scale",       "aiml_gltf_mat_normal_scale")
            GLTF_F32M(ci, "occlusion_strength", "aiml_gltf_mat_occlusion_strength")
            GLTF_F32M(ci, "emissive_strength",  "aiml_gltf_mat_emissive_strength")

            // texture_coord(slot) / texture_has_transform(slot): (mat, string) → i32
#define GLTF_SLOT(ci, aimlName, cName) \
            (ci).builtinMethods[aimlName] = [](CompilerContext& ctx, llvm::Value* base, \
                AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* { \
                auto* p   = llvm::PointerType::get(ctx.context, 0); \
                auto* i32 = ctx.builder.getInt32Ty(); \
                llvm::Value* slot = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p)); \
                if (call && !call->args.empty()) { \
                    IRValue* a = evalExpr(call->args[0].get()); \
                    if (a) slot = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a)); \
                } \
                return ctx.ir_gen->wrap(ctx.builder.CreateCall( \
                    ctx.getFunction(cName, i32, {p, p}), {base, slot})); \
            };
            GLTF_SLOT(ci, "texture_coord",         "aiml_gltf_mat_texture_coord")
            GLTF_SLOT(ci, "texture_has_transform", "aiml_gltf_mat_texture_has_transform")
#undef GLTF_SLOT

            ctx.classes["gltf_material"] = std::move(ci);
        }

        // ── gltf_texture ────────────────────────────────────────────────────
        {
            CompilerContext::ClassInfo ci;
            ci.name        = "gltf_texture";
            ci.isBuiltin   = true;
            ci.preLoadBase = true;

            GLTF_PTRM(ci, "image",      "aiml_gltf_tex_image")
            GLTF_I32M(ci, "image_index", "aiml_gltf_tex_image_index")
            GLTF_STR (ci, "wrap_s",     "aiml_gltf_tex_wrap_s")
            GLTF_STR (ci, "wrap_t",     "aiml_gltf_tex_wrap_t")
            GLTF_STR (ci, "min_filter", "aiml_gltf_tex_min_filter")
            GLTF_STR (ci, "mag_filter", "aiml_gltf_tex_mag_filter")

            ctx.classes["gltf_texture"] = std::move(ci);
        }

        // ── gltf_image ──────────────────────────────────────────────────────
        {
            CompilerContext::ClassInfo ci;
            ci.name        = "gltf_image";
            ci.isBuiltin   = true;
            ci.preLoadBase = true;

            GLTF_BUFM(ci, "pixels_rgba",    "aiml_gltf_img_pixels_rgba")
            GLTF_I32M(ci, "width",          "aiml_gltf_img_width")
            GLTF_I32M(ci, "height",         "aiml_gltf_img_height")
            GLTF_PTRM(ci, "to_gl_texture",  "aiml_gltf_img_to_gl_texture")

            ctx.classes["gltf_image"] = std::move(ci);
        }

        // ── gltf_animation ──────────────────────────────────────────────────
        {
            CompilerContext::ClassInfo ci;
            ci.name        = "gltf_animation";
            ci.isBuiltin   = true;
            ci.preLoadBase = true;

            GLTF_STR (ci, "name",     "aiml_gltf_anim_name")
            GLTF_F32M(ci, "duration", "aiml_gltf_anim_duration")

            // sample_translation_x/y/z and sample_rotation_x/y/z/w: (anim, node, t) → f32
#define GLTF_SAMPLE(ci, aimlName, cName) \
            (ci).builtinMethods[aimlName] = [](CompilerContext& ctx, llvm::Value* base, \
                AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* { \
                auto* p   = llvm::PointerType::get(ctx.context, 0); \
                auto* f32 = ctx.builder.getFloatTy(); \
                llvm::Value* nodeArg = llvm::ConstantPointerNull::get( \
                    llvm::cast<llvm::PointerType>(p)); \
                llvm::Value* tArg = llvm::ConstantFP::get(f32, 0.0); \
                if (call && call->args.size() >= 1) { \
                    IRValue* a = evalExpr(call->args[0].get()); \
                    if (a) nodeArg = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a)); \
                } \
                if (call && call->args.size() >= 2) { \
                    IRValue* a = evalExpr(call->args[1].get()); \
                    if (a) tArg = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a)); \
                } \
                return ctx.ir_gen->wrap(ctx.builder.CreateCall( \
                    ctx.getFunction(cName, f32, {p, p, f32}), {base, nodeArg, tArg})); \
            };

            GLTF_SAMPLE(ci, "sample_translation_x", "aiml_gltf_anim_sample_translation_x")
            GLTF_SAMPLE(ci, "sample_translation_y", "aiml_gltf_anim_sample_translation_y")
            GLTF_SAMPLE(ci, "sample_translation_z", "aiml_gltf_anim_sample_translation_z")
            GLTF_SAMPLE(ci, "sample_rotation_x",    "aiml_gltf_anim_sample_rotation_x")
            GLTF_SAMPLE(ci, "sample_rotation_y",    "aiml_gltf_anim_sample_rotation_y")
            GLTF_SAMPLE(ci, "sample_rotation_z",    "aiml_gltf_anim_sample_rotation_z")
            GLTF_SAMPLE(ci, "sample_rotation_w",    "aiml_gltf_anim_sample_rotation_w")
#undef GLTF_SAMPLE

            // spec/43_1 §8: (node, t) → owned buffer<f32>[16]
            ci.builtinMethods["sample_local_matrix"] = [](CompilerContext& ctx, llvm::Value* base,
                AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                auto* p   = llvm::PointerType::get(ctx.context, 0);
                auto* f32 = ctx.builder.getFloatTy();
                llvm::Value* nodeArg = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
                llvm::Value* tArg = llvm::ConstantFP::get(f32, 0.0);
                if (call && call->args.size() >= 1) {
                    IRValue* a = evalExpr(call->args[0].get());
                    if (a) nodeArg = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a));
                }
                if (call && call->args.size() >= 2) {
                    IRValue* a = evalExpr(call->args[1].get());
                    if (a) tArg = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a));
                }
                llvm::Value* hp = ctx.builder.CreateCall(
                    ctx.getFunction("aiml_gltf_anim_sample_local_matrix", p, {p, p, f32}), {base, nodeArg, tArg});
                return ctx.ir_gen->wrap(GLTF_HEAP_TO_ALLOCA(hp));
            };

            ctx.classes["gltf_animation"] = std::move(ci);
        }

        // ── gltf_skin ────────────────────────────────────────────────────────
        {
            CompilerContext::ClassInfo ci;
            ci.name        = "gltf_skin";
            ci.isBuiltin   = true;
            ci.preLoadBase = true;

            GLTF_I32M(ci, "joint_count",             "aiml_gltf_skin_joint_count")
            GLTF_BUFM(ci, "inverse_bind_matrices",   "aiml_gltf_skin_inverse_bind_matrices")
            GLTF_LIST(ci, "joints",                  "aiml_gltf_skin_joints_fill")

            ctx.classes["gltf_skin"] = std::move(ci);
        }

#undef GLTF_HEAP_TO_ALLOCA
#undef GLTF_STR
#undef GLTF_I32M
#undef GLTF_F32M
#undef GLTF_PTRM
#undef GLTF_BUFM
#undef GLTF_LIST

        // ── gltf namespace: gltf.load(path) → gltf_scene ───────────────────
        {
            auto& gltf = ctx.namespaces["gltf"];
            gltf.builtinFuncs["load"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                auto* p = llvm::PointerType::get(ctx.context, 0);
                // spec/44_7: a string LITERAL that lies in the asset bundle is
                // an error — the same rule as for gl.load_texture.  A fixed
                // path breaks as soon as someone changes the working directory or
                // builds for WASM.  Dynamic paths (command line) stay allowed.
                if (!args.empty() && ctx.currentCall && !ctx.currentCall->args.empty()) {
                    if (auto* lit = dynamic_cast<AIML::AST::LiteralExpr*>(
                            ctx.currentCall->args[0].get())) {
                        if (lit->type == "string" &&
                            AIML::AssetScanner::instance().path_under_root(lit->value)) {
                            auto dot   = lit->value.find_last_of('.');
                            auto slash = lit->value.find_last_of('/');
                            size_t a = (slash == std::string::npos) ? 0 : slash + 1;
                            size_t b = (dot == std::string::npos || dot < a)
                                       ? lit->value.size() : dot;
                            LOG_ERROR(ctx.fmtLoc() + std::string("gltf.load: literal path '") + lit->value +
                                      "' is in the asset bundle; use gltf.model(\"" +
                                      lit->value.substr(a, b - a) + "\") instead");
                        }
                    }
                }
                llvm::Value* pathArg = args.empty()
                    ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p))
                    : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                llvm::Value* scenePtr = ctx.builder.CreateCall(
                    ctx.getFunction("aiml_gltf_load", p, {p}), {pathArg});
                return ctx.ir_gen->wrap(scenePtr);
            };

            // gltf.model(stem) → gltf_scene (asset registry, spec/44_7).  The stem
            // is looked up in the mesh store, loading uses the same loader.
            // Literal stems are checked at compile time (like gl.tex).
            gltf.builtinFuncs["model"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                auto* p = llvm::PointerType::get(ctx.context, 0);
                if (!args.empty() && ctx.currentCall && !ctx.currentCall->args.empty()) {
                    if (auto* lit = dynamic_cast<AIML::AST::LiteralExpr*>(
                            ctx.currentCall->args[0].get())) {
                        if (lit->type == "string" &&
                            !AIML::AssetScanner::instance().has_stem(
                                AIML::AssetKind::Mesh, lit->value)) {
                            LOG_ERROR(ctx.fmtLoc() + std::string("gltf.model: stem '") +
                                      lit->value + "' not in mesh store");
                        }
                    }
                }
                llvm::Value* stemArg = args.empty()
                    ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p))
                    : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                llvm::Value* pathArg = ctx.builder.CreateCall(
                    ctx.getFunction("aiml_asset_mesh_path", p, {p}), {stemArg});
                llvm::Value* scenePtr = ctx.builder.CreateCall(
                    ctx.getFunction("aiml_gltf_load", p, {p}), {pathArg});
                return ctx.ir_gen->wrap(scenePtr);
            };
        }
    } // end gltf

    // =======================================================================
    // gl — OpenGL 3.3+ Core (GLEW backend)
    // Handle types: gl_buffer, gl_vao, gl_shader, gl_texture, gl_framebuffer
    // All handles are opaque i8* with preLoadBase = true.
    // =======================================================================
    {
        // ── Helper: store a loaded struct value to a temp alloca ──────────
#define GL_STORE_TMP(val, ty) \
        ([&]() -> llvm::Value* { \
            llvm::AllocaInst* _a = ctx.createEntryAlloca((ty), "gl_tmp"); \
            ctx.builder.CreateStore((val), _a); \
            return _a; \
        }())

        // ── Simple method macros (same style as gltf section) ─────────────
#define GL_VOID0(ci, aimlName, cName) \
        (ci).builtinMethods[aimlName] = [](CompilerContext& ctx, llvm::Value* base, \
            AIML::AST::CallExpr*, auto) -> IRValue* { \
            auto* p = llvm::PointerType::get(ctx.context, 0); \
            ctx.builder.CreateCall(ctx.getFunction(cName, ctx.builder.getVoidTy(), {p}), {base}); \
            return nullptr; \
        };

#define GL_PTR0(ci, aimlName, cName) \
        (ci).builtinMethods[aimlName] = [](CompilerContext& ctx, llvm::Value* base, \
            AIML::AST::CallExpr*, auto) -> IRValue* { \
            auto* p = llvm::PointerType::get(ctx.context, 0); \
            return ctx.ir_gen->wrap(ctx.builder.CreateCall( \
                ctx.getFunction(cName, p, {p}), {base})); \
        };

        // ── gl_buffer ───────────────────────────────────────────────────────
        {
            CompilerContext::ClassInfo ci;
            ci.name = "gl_buffer"; ci.isBuiltin = true; ci.preLoadBase = true;

            ci.builtinMethods["update"] = [](CompilerContext& ctx, llvm::Value* base,
                AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                auto* p  = llvm::PointerType::get(ctx.context, 0);
                auto* bt = ctx.getLLVMType("buffer");
                llvm::Value* dataArg = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
                if (call && !call->args.empty()) {
                    IRValue* a = evalExpr(call->args[0].get());
                    if (a) {
                        llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a));
                        if (v->getType()->isPointerTy()) dataArg = v;
                        else dataArg = GL_STORE_TMP(v, bt);
                    }
                }
                ctx.builder.CreateCall(ctx.getFunction("aiml_gl_buffer_update",
                    ctx.builder.getVoidTy(), {p, p}), {base, dataArg});
                return nullptr;
            };
            ci.builtinMethods["reserve_bytes"] = gl43Method("aiml_gl_buffer_reserve_bytes", "pi", 'z');
            ci.builtinMethods["update_range"] = gl43Method("aiml_gl_buffer_update_range", "pib", 'z');
            GL_VOID0(ci, "delete", "aiml_gl_buffer_delete")

            ctx.classes["gl_buffer"] = std::move(ci);
        }

        // ── gl_vao ──────────────────────────────────────────────────────────
        {
            CompilerContext::ClassInfo ci;
            ci.name = "gl_vao"; ci.isBuiltin = true; ci.preLoadBase = true;

            // bind_vbo / bind_ibo: (vao*, vbo*) → void
#define GL_VAO_BIND(aimlName, cName) \
            ci.builtinMethods[aimlName] = [](CompilerContext& ctx, llvm::Value* base, \
                AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* { \
                auto* p = llvm::PointerType::get(ctx.context, 0); \
                llvm::Value* arg = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p)); \
                if (call && !call->args.empty()) { \
                    IRValue* a = evalExpr(call->args[0].get()); \
                    if (a) arg = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a)); \
                } \
                ctx.builder.CreateCall(ctx.getFunction(cName, ctx.builder.getVoidTy(), {p, p}), \
                    {base, arg}); \
                return nullptr; \
            };
            GL_VAO_BIND("bind_vbo", "aiml_gl_vao_bind_vbo")
            GL_VAO_BIND("bind_ibo", "aiml_gl_vao_bind_ibo")
#undef GL_VAO_BIND

            // attrib / attrib_i: (vao*, idx, components, stride, offset) → void
#define GL_VAO_ATTRIB(aimlName, cName) \
            ci.builtinMethods[aimlName] = [](CompilerContext& ctx, llvm::Value* base, \
                AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* { \
                auto* p   = llvm::PointerType::get(ctx.context, 0); \
                auto* i32 = ctx.builder.getInt32Ty(); \
                auto g = [&](int i) -> llvm::Value* { \
                    if (!call || (int)call->args.size() <= i) return ctx.builder.getInt32(0); \
                    IRValue* a = evalExpr(call->args[i].get()); \
                    if (!a) return ctx.builder.getInt32(0); \
                    llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a)); \
                    return ctx.builder.CreateZExtOrTrunc(v, i32); \
                }; \
                ctx.builder.CreateCall( \
                    ctx.getFunction(cName, ctx.builder.getVoidTy(), {p, i32, i32, i32, i32}), \
                    {base, g(0), g(1), g(2), g(3)}); \
                return nullptr; \
            };
            GL_VAO_ATTRIB("attrib",   "aiml_gl_vao_attrib")
            GL_VAO_ATTRIB("attrib_i", "aiml_gl_vao_attrib_i")
#undef GL_VAO_ATTRIB

            ci.builtinMethods["attrib_divisor"] = gl43Method("aiml_gl_vao_attrib_divisor", "pii", 'z');
            GL_VOID0(ci, "delete", "aiml_gl_vao_delete")

            ctx.classes["gl_vao"] = std::move(ci);
        }

        // ── gl_shader ───────────────────────────────────────────────────────
        {
            CompilerContext::ClassInfo ci;
            ci.name = "gl_shader"; ci.isBuiltin = true; ci.preLoadBase = true;

            GL_VOID0(ci, "use", "aiml_gl_shader_use")

            // uniform_f32(name, v)
            ci.builtinMethods["uniform_f32"] = [](CompilerContext& ctx, llvm::Value* base,
                AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                auto* p   = llvm::PointerType::get(ctx.context, 0);
                auto* f32 = ctx.builder.getFloatTy();
                llvm::Value* nameArg = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
                llvm::Value* vArg    = llvm::ConstantFP::get(f32, 0.0);
                if (call && call->args.size() >= 1) {
                    IRValue* a = evalExpr(call->args[0].get());
                    if (a) nameArg = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a));
                }
                if (call && call->args.size() >= 2) {
                    IRValue* a = evalExpr(call->args[1].get());
                    if (a) {
                        llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a));
                        vArg = v->getType()->isFloatTy() ? v : ctx.builder.CreateFPCast(v, f32);
                    }
                }
                ctx.builder.CreateCall(ctx.getFunction("aiml_gl_shader_uniform_f32",
                    ctx.builder.getVoidTy(), {p, p, f32}), {base, nameArg, vArg});
                return nullptr;
            };

            // uniform_i32(name, v)
            ci.builtinMethods["uniform_i32"] = [](CompilerContext& ctx, llvm::Value* base,
                AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                auto* p   = llvm::PointerType::get(ctx.context, 0);
                auto* i32 = ctx.builder.getInt32Ty();
                llvm::Value* nameArg = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
                llvm::Value* vArg    = ctx.builder.getInt32(0);
                if (call && call->args.size() >= 1) {
                    IRValue* a = evalExpr(call->args[0].get());
                    if (a) nameArg = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a));
                }
                if (call && call->args.size() >= 2) {
                    IRValue* a = evalExpr(call->args[1].get());
                    if (a) {
                        llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a));
                        vArg = ctx.builder.CreateZExtOrTrunc(v, i32);
                    }
                }
                ctx.builder.CreateCall(ctx.getFunction("aiml_gl_shader_uniform_i32",
                    ctx.builder.getVoidTy(), {p, p, i32}), {base, nameArg, vArg});
                return nullptr;
            };

            // uniform_vec2/vec3/vec4: (name, struct_val) — struct stored to temp alloca
#define GL_UNI_VEC(aimlName, cName, structTyName) \
            ci.builtinMethods[aimlName] = [](CompilerContext& ctx, llvm::Value* base, \
                AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* { \
                auto* p   = llvm::PointerType::get(ctx.context, 0); \
                auto* sTy = ctx.getLLVMType(structTyName); \
                llvm::Value* nameArg = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p)); \
                llvm::Value* vPtr    = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p)); \
                if (call && call->args.size() >= 1) { \
                    IRValue* a = evalExpr(call->args[0].get()); \
                    if (a) nameArg = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a)); \
                } \
                if (call && call->args.size() >= 2) { \
                    IRValue* a = evalExpr(call->args[1].get()); \
                    if (a) { \
                        llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a)); \
                        vPtr = v->getType()->isPointerTy() ? v : GL_STORE_TMP(v, sTy); \
                    } \
                } \
                ctx.builder.CreateCall(ctx.getFunction(cName, ctx.builder.getVoidTy(), {p, p, p}), \
                    {base, nameArg, vPtr}); \
                return nullptr; \
            };
            GL_UNI_VEC("uniform_vec2", "aiml_gl_shader_uniform_vec2", "vec2")
            GL_UNI_VEC("uniform_vec3", "aiml_gl_shader_uniform_vec3", "vec3")
            GL_UNI_VEC("uniform_vec4", "aiml_gl_shader_uniform_vec4", "vec4")
#undef GL_UNI_VEC

            // uniform_mat4: (name, mat4_val)
            ci.builtinMethods["uniform_mat4"] = [](CompilerContext& ctx, llvm::Value* base,
                AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                auto* p    = llvm::PointerType::get(ctx.context, 0);
                auto* m4Ty = ctx.getLLVMType("mat4");
                llvm::Value* nameArg = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
                llvm::Value* mPtr    = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
                if (call && call->args.size() >= 1) {
                    IRValue* a = evalExpr(call->args[0].get());
                    if (a) nameArg = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a));
                }
                if (call && call->args.size() >= 2) {
                    IRValue* a = evalExpr(call->args[1].get());
                    if (a) {
                        llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a));
                        mPtr = v->getType()->isPointerTy() ? v : GL_STORE_TMP(v, m4Ty);
                    }
                }
                ctx.builder.CreateCall(ctx.getFunction("aiml_gl_shader_uniform_mat4",
                    ctx.builder.getVoidTy(), {p, p, p}), {base, nameArg, mPtr});
                return nullptr;
            };

            // uniform_mat4_array: (name, buffer<f32>, count)
            ci.builtinMethods["uniform_mat4_array"] = [](CompilerContext& ctx, llvm::Value* base,
                AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                auto* p   = llvm::PointerType::get(ctx.context, 0);
                auto* i32 = ctx.builder.getInt32Ty();
                auto* bt  = ctx.getLLVMType("buffer");
                llvm::Value* nameArg  = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
                llvm::Value* bufArg   = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
                llvm::Value* cntArg   = ctx.builder.getInt32(0);
                if (call && call->args.size() >= 1) {
                    IRValue* a = evalExpr(call->args[0].get()); if (a) nameArg = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a));
                }
                if (call && call->args.size() >= 2) {
                    IRValue* a = evalExpr(call->args[1].get());
                    if (a) {
                        llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a));
                        bufArg = v->getType()->isPointerTy() ? v : GL_STORE_TMP(v, bt);
                    }
                }
                if (call && call->args.size() >= 3) {
                    IRValue* a = evalExpr(call->args[2].get());
                    if (a) cntArg = ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a)), i32);
                }
                ctx.builder.CreateCall(ctx.getFunction("aiml_gl_shader_uniform_mat4_array",
                    ctx.builder.getVoidTy(), {p, p, p, i32}), {base, nameArg, bufArg, cntArg});
                return nullptr;
            };

            ci.builtinMethods["uniform_vec3_array"] = gl43Method("aiml_gl_shader_uniform_vec3_array", "ppbi", 'i');
            ci.builtinMethods["uniform_location"] = gl43Method("aiml_gl_shader_uniform_location", "pp", 'i');
            ci.builtinMethods["attrib_location"]  = gl43Method("aiml_gl_shader_attrib_location",  "pp", 'i');
            GL_VOID0(ci, "delete", "aiml_gl_shader_delete")

            ctx.classes["gl_shader"] = std::move(ci);
        }

        // ── gl_texture ──────────────────────────────────────────────────────
        {
            CompilerContext::ClassInfo ci;
            ci.name = "gl_texture"; ci.isBuiltin = true; ci.preLoadBase = true;

            // set_wrap(s, t)
            ci.builtinMethods["set_wrap"] = [](CompilerContext& ctx, llvm::Value* base,
                AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                auto* p   = llvm::PointerType::get(ctx.context, 0);
                auto* i32 = ctx.builder.getInt32Ty();
                auto g = [&](int i) -> llvm::Value* {
                    if (!call || (int)call->args.size() <= i) return ctx.builder.getInt32(0x2901 /*GL_REPEAT*/);
                    IRValue* a = evalExpr(call->args[i].get()); if (!a) return ctx.builder.getInt32(0x2901);
                    return ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a)), i32);
                };
                ctx.builder.CreateCall(ctx.getFunction("aiml_gl_texture_set_wrap",
                    ctx.builder.getVoidTy(), {p, i32, i32}), {base, g(0), g(1)});
                return nullptr;
            };

            // set_filter(min, mag)
            ci.builtinMethods["set_filter"] = [](CompilerContext& ctx, llvm::Value* base,
                AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                auto* p   = llvm::PointerType::get(ctx.context, 0);
                auto* i32 = ctx.builder.getInt32Ty();
                auto g = [&](int i) -> llvm::Value* {
                    if (!call || (int)call->args.size() <= i) return ctx.builder.getInt32(0x2601 /*GL_LINEAR*/);
                    IRValue* a = evalExpr(call->args[i].get()); if (!a) return ctx.builder.getInt32(0x2601);
                    return ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a)), i32);
                };
                ctx.builder.CreateCall(ctx.getFunction("aiml_gl_texture_set_filter",
                    ctx.builder.getVoidTy(), {p, i32, i32}), {base, g(0), g(1)});
                return nullptr;
            };

            GL_VOID0(ci, "gen_mipmaps", "aiml_gl_texture_gen_mipmaps")
            ci.builtinMethods["width"]  = gl43Method("aiml_gl_texture_width",  "p", 'i');
            ci.builtinMethods["height"] = gl43Method("aiml_gl_texture_height", "p", 'i');
            ci.builtinMethods["format"] = gl43Method("aiml_gl_texture_format", "p", 'i');
            ci.builtinMethods["levels"] = gl43Method("aiml_gl_texture_levels", "p", 'i');
            GL_VOID0(ci, "delete",      "aiml_gl_texture_release")   // spec/43_2 §5.5 early close

            ctx.classes["gl_texture"] = std::move(ci);
        }

        // ── gl_cubemap (spec/43_5 §5) ───────────────────────────────────────
        {
            CompilerContext::ClassInfo ci;
            ci.name = "gl_cubemap"; ci.isBuiltin = true; ci.preLoadBase = true;
            ci.builtinMethods["upload_face"] = gl43Method("aiml_gl_cubemap_upload_face", "piib", 'i');
            GL_VOID0(ci, "delete", "aiml_gl_cubemap_release")
            ctx.classes["gl_cubemap"] = std::move(ci);
        }

        // ── gl_timer_query (spec/43_5 §9) ───────────────────────────────────
        {
            CompilerContext::ClassInfo ci;
            ci.name = "gl_timer_query"; ci.isBuiltin = true; ci.preLoadBase = true;
            ci.builtinMethods["begin"]     = gl43Method("aiml_gl_timer_query_begin", "p", 'z');
            ci.builtinMethods["end"]       = gl43Method("aiml_gl_timer_query_end", "p", 'z');
            ci.builtinMethods["ready"]     = gl43Method("aiml_gl_timer_query_ready", "p", 'z');
            ci.builtinMethods["result_ns"] = gl43Method("aiml_gl_timer_query_result_ns", "p", 'l');
            GL_VOID0(ci, "delete", "aiml_gl_timer_query_release")
            ctx.classes["gl_timer_query"] = std::move(ci);
        }

        // ── gl_framebuffer ──────────────────────────────────────────────────
        {
            CompilerContext::ClassInfo ci;
            ci.name = "gl_framebuffer"; ci.isBuiltin = true; ci.preLoadBase = true;

            GL_PTR0(ci, "color_texture", "aiml_gl_framebuffer_color_texture")

            ci.builtinMethods["resize"] = [](CompilerContext& ctx, llvm::Value* base,
                AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                auto* p   = llvm::PointerType::get(ctx.context, 0);
                auto* i32 = ctx.builder.getInt32Ty();
                auto g = [&](int i) -> llvm::Value* {
                    if (!call || (int)call->args.size() <= i) return ctx.builder.getInt32(0);
                    IRValue* a = evalExpr(call->args[i].get()); if (!a) return ctx.builder.getInt32(0);
                    return ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a)), i32);
                };
                ctx.builder.CreateCall(ctx.getFunction("aiml_gl_framebuffer_resize",
                    ctx.builder.getVoidTy(), {p, i32, i32}), {base, g(0), g(1)});
                return nullptr;
            };
            ci.builtinMethods["bind_color_texture"] = gl43Method("aiml_gl_framebuffer_bind_color_texture", "pi", 'v');
            ci.builtinMethods["resize_checked"] = gl43Method("aiml_gl_framebuffer_resize_checked", "pii", 'i');
            ci.builtinMethods["width"]  = gl43Method("aiml_gl_framebuffer_width",  "p", 'i');
            ci.builtinMethods["height"] = gl43Method("aiml_gl_framebuffer_height", "p", 'i');
            ci.builtinMethods["format"] = gl43Method("aiml_gl_framebuffer_format", "p", 'i');
            GL_VOID0(ci, "delete", "aiml_gl_framebuffer_delete")

            ctx.classes["gl_framebuffer"] = std::move(ci);
        }

#undef GL_STORE_TMP
#undef GL_VOID0
#undef GL_PTR0

        // ── gl namespace functions ──────────────────────────────────────────
        {
            auto& gl = ctx.namespaces["gl"];

            // Zero-arg void functions
#define GL_NS_VOID0(name, cName) \
            gl.builtinFuncs[name] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>&) -> AIML::IRValue* { \
                ctx.builder.CreateCall(ctx.getFunction(cName, ctx.builder.getVoidTy(), {}), {}); \
                return nullptr; \
            };
            GL_NS_VOID0("init",               "aiml_gl_init")
            GL_NS_VOID0("clear_depth",         "aiml_gl_clear_depth")
            GL_NS_VOID0("enable_depth_test",   "aiml_gl_enable_depth_test")
            GL_NS_VOID0("disable_depth_test",  "aiml_gl_disable_depth_test")
            GL_NS_VOID0("enable_blend",        "aiml_gl_enable_blend")
            GL_NS_VOID0("disable_blend",       "aiml_gl_disable_blend")
            GL_NS_VOID0("enable_cull_face",    "aiml_gl_enable_cull_face")
            GL_NS_VOID0("disable_cull_face",   "aiml_gl_disable_cull_face")
            GL_NS_VOID0("cull_back",           "aiml_gl_cull_back")
            GL_NS_VOID0("cull_front",          "aiml_gl_cull_front")
            GL_NS_VOID0("bind_default_framebuffer", "aiml_gl_bind_default_framebuffer")
            GL_NS_VOID0("create_vao",          "aiml_gl_create_vao")  /* overridden below */
#undef GL_NS_VOID0

            // gl.viewport(x, y, w, h: i32) → void
            gl.builtinFuncs["viewport"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                auto* i32 = ctx.builder.getInt32Ty();
                auto g = [&](int i) -> llvm::Value* {
                    if ((int)args.size() <= i) return ctx.builder.getInt32(0);
                    return ctx.builder.CreateZExtOrTrunc(
                        static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])), i32);
                };
                ctx.builder.CreateCall(ctx.getFunction("aiml_gl_viewport",
                    ctx.builder.getVoidTy(), {i32, i32, i32, i32}), {g(0), g(1), g(2), g(3)});
                return nullptr;
            };

            // gl.clear(r, g, b, a: f32) / gl.clear_all(r, g, b, a)
#define GL_NS_CLEAR(name, cName) \
            gl.builtinFuncs[name] = [](CompilerContext& ctx, \
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* { \
                auto* f32 = ctx.builder.getFloatTy(); \
                auto g = [&](int i) -> llvm::Value* { \
                    if ((int)args.size() <= i) return llvm::ConstantFP::get(f32, 0.0); \
                    llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])); \
                    return v->getType()->isFloatTy() ? v : ctx.builder.CreateFPCast(v, f32); \
                }; \
                ctx.builder.CreateCall(ctx.getFunction(cName, ctx.builder.getVoidTy(), {f32,f32,f32,f32}), \
                    {g(0), g(1), g(2), g(3)}); \
                return nullptr; \
            };
            GL_NS_CLEAR("clear",     "aiml_gl_clear")
            GL_NS_CLEAR("clear_all", "aiml_gl_clear_all")
#undef GL_NS_CLEAR

            // gl.wireframe(on: i8) → void
            gl.builtinFuncs["wireframe"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                llvm::Value* on = args.empty() ? ctx.builder.getInt8(0)
                    : ctx.builder.CreateZExtOrTrunc(
                        static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                        ctx.builder.getInt8Ty());
                ctx.builder.CreateCall(ctx.getFunction("aiml_gl_wireframe",
                    ctx.builder.getVoidTy(), {ctx.builder.getInt8Ty()}), {on});
                return nullptr;
            };

            // gl.polygon_offset(factor, units: f32) → void
            gl.builtinFuncs["polygon_offset"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                auto* f32 = ctx.builder.getFloatTy();
                auto g = [&](int i) -> llvm::Value* {
                    if ((int)args.size() <= i) return llvm::ConstantFP::get(f32, 0.0);
                    llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]));
                    return v->getType()->isFloatTy() ? v : ctx.builder.CreateFPCast(v, f32);
                };
                ctx.builder.CreateCall(ctx.getFunction("aiml_gl_polygon_offset",
                    ctx.builder.getVoidTy(), {f32, f32}), {g(0), g(1)});
                return nullptr;
            };
            // gl.enable_polygon_offset_fill() / disable_polygon_offset_fill()
#define GL_NS_VOID0_INLINE(name, cName) \
            gl.builtinFuncs[name] = [](CompilerContext& ctx, \
                const std::vector<AIML::IRValue*>&) -> AIML::IRValue* { \
                ctx.builder.CreateCall(ctx.getFunction(cName, \
                    ctx.builder.getVoidTy(), {}), {}); \
                return nullptr; \
            };
            GL_NS_VOID0_INLINE("enable_polygon_offset_fill",  "aiml_gl_enable_polygon_offset_fill")
            GL_NS_VOID0_INLINE("disable_polygon_offset_fill", "aiml_gl_disable_polygon_offset_fill")
#undef GL_NS_VOID0_INLINE

            // gl.create_buffer / create_buffer_dynamic / create_index_buffer: (data_buf) → gl_buffer
#define GL_NS_MKBUF(name, cName) \
            gl.builtinFuncs[name] = [](CompilerContext& ctx, \
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* { \
                auto* p  = llvm::PointerType::get(ctx.context, 0); \
                auto* bt = ctx.getLLVMType("buffer"); \
                llvm::Value* arg = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p)); \
                if (!args.empty()) { \
                    llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])); \
                    arg = v->getType()->isPointerTy() ? v : \
                          ([&]() -> llvm::Value* { \
                              auto* _a = ctx.createEntryAlloca(bt, "gl_buf_in"); \
                              ctx.builder.CreateStore(v, _a); return _a; }()); \
                } \
                return ctx.ir_gen->wrap(ctx.builder.CreateCall( \
                    ctx.getFunction(cName, p, {p}), {arg})); \
            };
            GL_NS_MKBUF("create_buffer",         "aiml_gl_create_buffer")
            GL_NS_MKBUF("create_buffer_dynamic",  "aiml_gl_create_buffer_dynamic")
            GL_NS_MKBUF("create_index_buffer",    "aiml_gl_create_index_buffer")
#undef GL_NS_MKBUF

            // gl.create_vao() → gl_vao
            gl.builtinFuncs["create_vao"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
                auto* p = llvm::PointerType::get(ctx.context, 0);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction("aiml_gl_create_vao", p, {}), {}));
            };

            // gl.compile_shader(vert_src, frag_src) → gl_shader
            gl.builtinFuncs["compile_shader"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                auto* p = llvm::PointerType::get(ctx.context, 0);
                auto null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
                llvm::Value* vs = args.size() > 0 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])) : null;
                llvm::Value* fs = args.size() > 1 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])) : null;
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction("aiml_gl_compile_shader", p, {p, p}), {vs, fs}));
            };

            // gl.load_shader(vert_path, frag_path) → gl_shader
            gl.builtinFuncs["load_shader"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                auto* p = llvm::PointerType::get(ctx.context, 0);
                auto null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
                llvm::Value* vp = args.size() > 0 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])) : null;
                llvm::Value* fp = args.size() > 1 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])) : null;
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction("aiml_gl_load_shader", p, {p, p}), {vp, fp}));
            };

            // gl.load_texture(path) → gl_texture.  Escape-Hatch for runtime-
            // generated paths.  Spec/22: when arg is a string literal AND the
            // path resolves under an `asset_directory`, emit a hard error
            // forcing the caller to switch to gl.tex(stem).
            gl.builtinFuncs["load_texture"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                auto* p = llvm::PointerType::get(ctx.context, 0);
                if (!args.empty()) {
                    if (auto* lit = dynamic_cast<AIML::AST::LiteralExpr*>(
                            ctx.currentCall ? ctx.currentCall->args[0].get() : nullptr)) {
                        if (lit->type == "string" &&
                            AIML::AssetScanner::instance().path_under_root(lit->value)) {
                            // Suggest the stem for the user's existing path.
                            std::string suggest;
                            auto dot = lit->value.find_last_of('.');
                            auto slash = lit->value.find_last_of('/');
                            size_t a = (slash == std::string::npos) ? 0 : slash + 1;
                            size_t b = (dot == std::string::npos || dot < a)
                                       ? lit->value.size() : dot;
                            suggest = lit->value.substr(a, b - a);
                            LOG_ERROR(ctx.fmtLoc() + std::string("gl.load_texture: literal path '") +
                                      lit->value + "' is in the asset bundle; use gl.tex(\"" +
                                      suggest + "\") instead");
                        }
                    }
                }
                llvm::Value* arg = args.empty()
                    ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p))
                    : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction("aiml_gl_load_texture", p, {p}), {arg}));
            };

            // gl.tex(stem) → gl_texture (Asset Registry, spec/44_7).  String-
            // literal arg gets compile-time validation against the texture
            // store; dynamic args fall through and runtime-error on miss.
            gl.builtinFuncs["tex"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                auto* p = llvm::PointerType::get(ctx.context, 0);
                if (!args.empty() && ctx.currentCall &&
                    !ctx.currentCall->args.empty()) {
                    if (auto* lit = dynamic_cast<AIML::AST::LiteralExpr*>(
                            ctx.currentCall->args[0].get())) {
                        if (lit->type == "string" &&
                            !AIML::AssetScanner::instance().has_stem(
                                AIML::AssetKind::Texture, lit->value)) {
                            LOG_ERROR(ctx.fmtLoc() + std::string("gl.tex: stem '") +
                                      lit->value + "' not in texture store");
                        }
                    }
                }
                llvm::Value* arg = args.empty()
                    ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p))
                    : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction("aiml_asset_get_texture", p, {p}), {arg}));
            };

            // gl.font(stem, size) → ui_font (spec/44_7).  Stem literal check
            // like gl.tex; size may be dynamic.  Backend: aiml_ui_load_font
            // via the asset resolver with a per-(stem,size) cache.
            gl.builtinFuncs["font"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                auto* p    = llvm::PointerType::get(ctx.context, 0);
                auto* f32  = ctx.builder.getFloatTy();
                if (!args.empty() && ctx.currentCall &&
                    !ctx.currentCall->args.empty()) {
                    if (auto* lit = dynamic_cast<AIML::AST::LiteralExpr*>(
                            ctx.currentCall->args[0].get())) {
                        if (lit->type == "string" &&
                            !AIML::AssetScanner::instance().has_stem(
                                AIML::AssetKind::Font, lit->value)) {
                            LOG_ERROR(ctx.fmtLoc() + std::string("gl.font: stem '") +
                                      lit->value + "' not in font store");
                        }
                    }
                }
                llvm::Value* stem = args.empty()
                    ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p))
                    : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                llvm::Value* sz   = (args.size() < 2)
                    ? llvm::ConstantFP::get(f32, 12.0)
                    : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
                // Promote double → float if needed (literals come as f64).
                if (sz->getType()->isDoubleTy()) sz = ctx.builder.CreateFPTrunc(sz, f32);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction("aiml_asset_get_font", p, {p, f32}), {stem, sz}));
            };

            // gl.create_texture(w, h, px_buf) → gl_texture
            gl.builtinFuncs["create_texture"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                auto* p   = llvm::PointerType::get(ctx.context, 0);
                auto* i32 = ctx.builder.getInt32Ty();
                auto* bt  = ctx.getLLVMType("buffer");
                auto g32 = [&](int i) -> llvm::Value* {
                    if ((int)args.size() <= i) return ctx.builder.getInt32(0);
                    return ctx.builder.CreateZExtOrTrunc(
                        static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])), i32);
                };
                llvm::Value* pxArg = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
                if (args.size() >= 3) {
                    llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]));
                    pxArg = v->getType()->isPointerTy() ? v :
                            ([&]() -> llvm::Value* {
                                auto* _a = ctx.createEntryAlloca(bt, "gl_px_in");
                                ctx.builder.CreateStore(v, _a); return _a; }());
                }
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction("aiml_gl_create_texture", p, {i32, i32, p}),
                    {g32(0), g32(1), pxArg}));
            };

            // gl.register_texture(stem, tex) → void — register a decoded texture
            // under a synthetic stem ("@art:<hash>") so a sprite's texture_path
            // resolves to it via the asset registry / load_texture_cached.
            gl.builtinFuncs["register_texture"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                auto* p = llvm::PointerType::get(ctx.context, 0);
                if (args.size() < 2) return ctx.ir_gen->getInt32(0);
                ctx.builder.CreateCall(
                    ctx.getFunction("aiml_asset_register_texture", ctx.builder.getVoidTy(), {p, p}),
                    { static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                      static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])) });
                return ctx.ir_gen->getInt32(0);
            };

            // gl.has_texture(stem) → i32 — 1 if a texture is cached under this stem.
            gl.builtinFuncs["has_texture"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                auto* p   = llvm::PointerType::get(ctx.context, 0);
                auto* i32 = ctx.builder.getInt32Ty();
                if (args.empty()) return ctx.ir_gen->getInt32(0);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction("aiml_asset_has_texture", i32, {p}),
                    { static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])) }));
            };

            // gl.decode_texture(webp: buffer) → gl_texture — decode encoded image
            // bytes (WebP/PNG/JPG) in `webp` into a GPU texture.  Native uses the
            // stb/libwebp path; WASM decodes in the browser (createImageBitmap).
            // Returns a gl_texture handle (null on failure).  Needs a live GL
            // context (client render path, not headless).
            gl.builtinFuncs["decode_texture"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                auto* p  = llvm::PointerType::get(ctx.context, 0);
                auto* bt = ctx.getLLVMType("buffer");
                if (args.empty())
                    return ctx.ir_gen->wrap(llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p)));
                // aiml_art_decode_texture reads ->data/->size, so it needs the
                // buffer's ADDRESS — pass an incoming pointer through, else spill
                // the by-value struct to a temp alloca (mirrors gl.create_texture).
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                llvm::Value* bufPtr = v->getType()->isPointerTy() ? v :
                    ([&]() -> llvm::Value* {
                        auto* _a = ctx.createEntryAlloca(bt, "art_dec_in");
                        ctx.builder.CreateStore(v, _a); return _a; }());
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction("aiml_art_decode_texture", p, {p}), {bufPtr}));
            };

            // gl.pick_source() → i32 — open a file picker + decode + stash the full
            // image as the pan-crop source (for source_texture / buffer.encode_source).
            // 1 = ok, 0 = cancelled/failed.  Needs a display (client path).
            gl.builtinFuncs["pick_source"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
                auto* i32 = ctx.builder.getInt32Ty();
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction("aiml_art_pick_source", i32, {}), {}));
            };

            // gl.source_texture(dw, dh, mode, panx, pany) → gl_texture — crop the
            // stashed source (mode 0 cover / 1 stretch) at pan (0..1 per axis) into a
            // dw×dh preview texture.  For live drag-to-position.  null on failure.
            gl.builtinFuncs["source_texture"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                auto* p   = llvm::PointerType::get(ctx.context, 0);
                auto* i32 = ctx.builder.getInt32Ty();
                auto* f32 = ctx.builder.getFloatTy();
                auto gi = [&](int i) -> llvm::Value* {
                    if ((int)args.size() <= i) return ctx.builder.getInt32(0);
                    return ctx.builder.CreateZExtOrTrunc(
                        static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])), i32);
                };
                auto gf = [&](int i) -> llvm::Value* {
                    if ((int)args.size() <= i) return llvm::ConstantFP::get(f32, 0.5);
                    llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]));
                    if (v->getType()->isIntegerTy()) return ctx.builder.CreateSIToFP(v, f32);
                    if (v->getType()->isDoubleTy())  return ctx.builder.CreateFPCast(v, f32);
                    return v;
                };
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction("aiml_art_source_texture", p, {i32, i32, i32, f32, f32}),
                    {gi(0), gi(1), gi(2), gf(3), gf(4)}));
            };

            // gl.bind_texture(tex, slot) → void
            gl.builtinFuncs["bind_texture"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                auto* p   = llvm::PointerType::get(ctx.context, 0);
                auto* i32 = ctx.builder.getInt32Ty();
                llvm::Value* tex  = args.size() > 0
                    ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))
                    : llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
                llvm::Value* slot = args.size() > 1
                    ? ctx.builder.CreateZExtOrTrunc(
                        static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])), i32)
                    : ctx.builder.getInt32(0);
                ctx.builder.CreateCall(ctx.getFunction("aiml_gl_bind_texture",
                    ctx.builder.getVoidTy(), {p, i32}), {tex, slot});
                return nullptr;
            };

            // gl.draw_arrays(vao, count) / gl.draw_elements(vao, count)
#define GL_NS_DRAW(name, cName) \
            gl.builtinFuncs[name] = [](CompilerContext& ctx, \
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* { \
                auto* p   = llvm::PointerType::get(ctx.context, 0); \
                auto* i32 = ctx.builder.getInt32Ty(); \
                llvm::Value* vao = args.size() > 0 \
                    ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])) \
                    : llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p)); \
                llvm::Value* cnt = args.size() > 1 \
                    ? ctx.builder.CreateZExtOrTrunc( \
                        static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])), i32) \
                    : ctx.builder.getInt32(0); \
                ctx.builder.CreateCall(ctx.getFunction(cName, ctx.builder.getVoidTy(), {p, i32}), \
                    {vao, cnt}); \
                return nullptr; \
            };
            GL_NS_DRAW("draw_arrays",   "aiml_gl_draw_arrays")
            GL_NS_DRAW("draw_elements", "aiml_gl_draw_elements")
            gl.builtinFuncs["draw_elements_instanced"] = gl43Fn("aiml_gl_draw_elements_instanced", "pii", 'z');
            GL_NS_DRAW("draw_lines",    "aiml_gl_draw_lines")
#undef GL_NS_DRAW

            // gl.draw_arrays_mode(vao, mode, count) → void
            gl.builtinFuncs["draw_arrays_mode"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                auto* p   = llvm::PointerType::get(ctx.context, 0);
                auto* i32 = ctx.builder.getInt32Ty();
                llvm::Value* vao  = args.size() > 0 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])) : llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
                llvm::Value* mode = args.size() > 1 ? ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])), i32) : ctx.builder.getInt32(0x0004);
                llvm::Value* cnt  = args.size() > 2 ? ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2])), i32) : ctx.builder.getInt32(0);
                ctx.builder.CreateCall(ctx.getFunction("aiml_gl_draw_arrays_mode",
                    ctx.builder.getVoidTy(), {p, i32, i32}), {vao, mode, cnt});
                return nullptr;
            };

            // gl.create_framebuffer(w, h) → gl_framebuffer
            gl.builtinFuncs["create_framebuffer"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                auto* p   = llvm::PointerType::get(ctx.context, 0);
                auto* i32 = ctx.builder.getInt32Ty();
                auto g32 = [&](int i) -> llvm::Value* {
                    if ((int)args.size() <= i) return ctx.builder.getInt32(0);
                    return ctx.builder.CreateZExtOrTrunc(
                        static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])), i32);
                };
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction("aiml_gl_create_framebuffer", p, {i32, i32}), {g32(0), g32(1)}));
            };

            // spec/43_5 §4/§6/§7/§9
            gl.builtinFuncs["create_texture_format"]     = gl43Fn("aiml_gl_create_texture_format", "iibii", 'p');
            gl.builtinFuncs["create_texture_f32"]        = gl43Fn("aiml_gl_create_texture_f32", "iibi", 'p');
            gl.builtinFuncs["create_framebuffer_format"] = gl43Fn("aiml_gl_create_framebuffer_format", "iiii", 'p');
            gl.builtinFuncs["depth_write"]         = gl43Fn("aiml_gl_depth_write", "i", 'v');
            gl.builtinFuncs["depth_func"]          = gl43Fn("aiml_gl_depth_func", "i", 'v');
            gl.builtinFuncs["front_face"]          = gl43Fn("aiml_gl_front_face", "i", 'v');
            gl.builtinFuncs["blend_func_separate"] = gl43Fn("aiml_gl_blend_func_separate", "iiii", 'v');
            gl.builtinFuncs["framebuffer_srgb"]    = gl43Fn("aiml_gl_framebuffer_srgb", "i", 'i');
            gl.builtinFuncs["supports"]            = gl43Fn("aiml_gl_supports", "p", 'i');
            gl.builtinFuncs["limit"]               = gl43Fn("aiml_gl_limit", "p", 'i');
            gl.builtinFuncs["read_rgba8"]          = gl43Fn("aiml_gl_read_rgba8", "iiii", 'B');
            gl.builtinFuncs["create_cubemap"]      = gl43Fn("aiml_gl_create_cubemap", "ii", 'p');
            gl.builtinFuncs["create_timer_query"]  = gl43Fn("aiml_gl_create_timer_query", "", 'p');
            gl.builtinFuncs["bind_cubemap"]        = gl43Fn("aiml_gl_bind_cubemap", "pi", 'v');

            // gl.bind_framebuffer(fbo) → void
            gl.builtinFuncs["bind_framebuffer"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                auto* p = llvm::PointerType::get(ctx.context, 0);
                llvm::Value* fb = args.empty()
                    ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p))
                    : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                ctx.builder.CreateCall(ctx.getFunction("aiml_gl_bind_framebuffer",
                    ctx.builder.getVoidTy(), {p}), {fb});
                return nullptr;
            };
        }

        // ── gl_mesh ─────────────────────────────────────────────────────────
        {
            CompilerContext::ClassInfo ci;
            ci.name = "gl_mesh"; ci.isBuiltin = true; ci.preLoadBase = true;

            // mesh.draw() → void
            ci.builtinMethods["draw"] = [](CompilerContext& ctx, llvm::Value* base,
                AIML::AST::CallExpr*, auto) -> IRValue* {
                auto* p = llvm::PointerType::get(ctx.context, 0);
                ctx.builder.CreateCall(ctx.getFunction("aiml_gl_mesh_draw",
                    ctx.builder.getVoidTy(), {p}), {base});
                return nullptr;
            };
            // mesh.draw_count(n: i32) → void
            ci.builtinMethods["draw_count"] = [](CompilerContext& ctx, llvm::Value* base,
                AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                auto* p   = llvm::PointerType::get(ctx.context, 0);
                auto* i32 = ctx.builder.getInt32Ty();
                llvm::Value* cnt = ctx.builder.getInt32(0);
                if (call && !call->args.empty()) {
                    IRValue* a = evalExpr(call->args[0].get());
                    if (a) cnt = ctx.builder.CreateZExtOrTrunc(
                        static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a)), i32);
                }
                ctx.builder.CreateCall(ctx.getFunction("aiml_gl_mesh_draw_count",
                    ctx.builder.getVoidTy(), {p, i32}), {base, cnt});
                return nullptr;
            };
            // mesh.index_count() → i32
            ci.builtinMethods["index_count"] = [](CompilerContext& ctx, llvm::Value* base,
                AIML::AST::CallExpr*, auto) -> IRValue* {
                auto* p = llvm::PointerType::get(ctx.context, 0);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction("aiml_gl_mesh_index_count",
                        ctx.builder.getInt32Ty(), {p}), {base}));
            };
            // mesh.delete() → void
            ci.builtinMethods["delete"] = [](CompilerContext& ctx, llvm::Value* base,
                AIML::AST::CallExpr*, auto) -> IRValue* {
                auto* p = llvm::PointerType::get(ctx.context, 0);
                ctx.builder.CreateCall(ctx.getFunction("aiml_gl_mesh_delete",
                    ctx.builder.getVoidTy(), {p}), {base});
                return nullptr;
            };
            // mesh.set_attribute(location: i32, components: i32, data: buffer<f32>) → bool (spec/43_2 §3.4)
            ci.builtinMethods["set_attribute"] = [](CompilerContext& ctx, llvm::Value* base,
                AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                auto* p   = llvm::PointerType::get(ctx.context, 0);
                auto* i32 = ctx.builder.getInt32Ty();
                if (!call || call->args.size() != 3) {
                    LOG_ERROR(std::string(call ? ctx.fmtLoc(call->sourceNode) : "") + "gl_mesh.set_attribute expects (location: i32, components: i32, data: buffer<f32>)");
                    return nullptr;
                }
                IRValue* a0 = evalExpr(call->args[0].get());
                IRValue* a1 = evalExpr(call->args[1].get());
                IRValue* a2 = evalExpr(call->args[2].get());
                if (!a0 || !a1 || !a2) return nullptr;
                llvm::Value* loc  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a0));
                llvm::Value* comp = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a1));
                llvm::Value* data = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a2));
                if (data->getType()->isStructTy()) {
                    /* buffer<T> struct value — store to an alloca and pass its address (as gl.create_mesh) */
                    auto* tmp = ctx.createEntryAlloca(data->getType(), "attr_buf");
                    ctx.builder.CreateStore(data, tmp);
                    data = tmp;
                }
                if (!loc->getType()->isIntegerTy() || !comp->getType()->isIntegerTy() || !data->getType()->isPointerTy()) {
                    LOG_ERROR(ctx.fmtLoc(call->sourceNode) + "gl_mesh.set_attribute expects (location: i32, components: i32, data: buffer<f32>)");
                    return nullptr;
                }
                loc  = ctx.builder.CreateSExtOrTrunc(loc, i32);
                comp = ctx.builder.CreateSExtOrTrunc(comp, i32);
                llvm::Value* res = ctx.builder.CreateCall(ctx.getFunction("aiml_gl_mesh_set_attribute",
                    i32, {p, i32, i32, p}), {base, loc, comp, data});
                return ctx.ir_gen->wrap(ctx.builder.CreateICmpNE(res, ctx.builder.getInt32(0)));
            };
            ctx.classes["gl_mesh"] = std::move(ci);
        }

        // ── gl.create_mesh ──────────────────────────────────────────────────
        {
            auto& gl = ctx.namespaces["gl"];
            gl.builtinFuncs["create_mesh"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                auto* p   = llvm::PointerType::get(ctx.context, 0);
                auto null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
                auto getArg = [&](int i) -> llvm::Value* {
                    if ((int)args.size() <= i) return null;
                    llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]));
                    if (v->getType()->isPointerTy()) return v;
                    /* buffer<T> struct — store to alloca and pass as pointer */
                    auto* tmp = ctx.createEntryAlloca(v->getType(), "mesh_buf");
                    ctx.builder.CreateStore(v, tmp); return tmp;
                };
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction("aiml_gl_create_mesh", p, {p, p, p, p}),
                    {getArg(0), getArg(1), getArg(2), getArg(3)}));
            };
        }
    } // end gl

    // ==========================================================================
    // engine — cross-platform window + game loop
    // ==========================================================================
    {
        // ── window_handle type ───────────────────────────────────────────────
        {
            CompilerContext::ClassInfo ci;
            ci.name = "window_handle"; ci.isBuiltin = true; ci.preLoadBase = true;
            // No methods needed on window_handle directly — use engine.* namespace
            ctx.classes["window_handle"] = std::move(ci);
        }

        auto& eng = ctx.namespaces["engine"];
        auto* p   = llvm::PointerType::get(ctx.context, 0);
        auto* i32 = ctx.builder.getInt32Ty();
        auto* f32 = ctx.builder.getFloatTy();
        auto* f64 = ctx.builder.getDoubleTy();
        auto* i64 = ctx.builder.getInt64Ty();
        auto* vd  = ctx.builder.getVoidTy();
        auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        auto null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));

#define ENG_WIN_RET_PTR(name, cName) \
        eng.builtinFuncs[name] = [](CompilerContext& ctx, \
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* { \
            auto* p   = llvm::PointerType::get(ctx.context, 0); \
            auto* i32 = ctx.builder.getInt32Ty(); \
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo(); \
            auto null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p)); \
            auto g = [&](int i) -> llvm::Value* { \
                if ((int)args.size() <= i) return null; \
                return static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])); \
            }; \
            (void)i32; (void)i8p; (void)null; (void)g; \
            return ctx.ir_gen->wrap(ctx.builder.CreateCall( \
                ctx.getFunction(cName, p, {i32, i32, i8p}), \
                {ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])), i32), \
                 ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])), i32), \
                 args.size() > 2 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2])) \
                                 : llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p))})); \
        };

        // engine.create_window(w, h, title) → window_handle
        ENG_WIN_RET_PTR("create_window", "aiml_engine_create_window")
#undef ENG_WIN_RET_PTR

        // engine.create_window_fullscreen(title) → window_handle
        eng.builtinFuncs["create_window_fullscreen"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* title = args.empty()
                ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p))
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_engine_create_window_fullscreen", p, {i8p}), {title}));
        };

        // engine.destroy_window(win) → void
        eng.builtinFuncs["destroy_window"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* w = args.empty()
                ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p))
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            ctx.builder.CreateCall(ctx.getFunction("aiml_engine_destroy_window",
                ctx.builder.getVoidTy(), {p}), {w});
            return nullptr;
        };

        // engine.set_title(win, title)
        eng.builtinFuncs["set_title"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* w = args.size() > 0 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))
                : llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            llvm::Value* t = args.size() > 1 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
                : llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p));
            ctx.builder.CreateCall(ctx.getFunction("aiml_engine_set_title",
                ctx.builder.getVoidTy(), {p, i8p}), {w, t});
            return nullptr;
        };

        // engine.maximize(win) — System-Maximize-State setzen.
        eng.builtinFuncs["maximize"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* w = args.empty()
                ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p))
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            ctx.builder.CreateCall(ctx.getFunction("aiml_engine_maximize_window",
                ctx.builder.getVoidTy(), {p}), {w});
            return nullptr;
        };

        // engine.window_size(win) → ivec2 (spec/44_1 §2); _x/_y/_xf/_yf are scalar shortcuts.
        // engine.window_pos(win) → ivec2: window position on the screen.
        // For converting a widget area → screen coordinate (spec/44_0 §8.5).
        eng.builtinFuncs["window_pos"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* w = args.empty()
                ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p))
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* px = ctx.builder.CreateCall(ctx.getFunction("aiml_engine_window_pos_x", i32, {p}), {w});
            llvm::Value* py = ctx.builder.CreateCall(ctx.getFunction("aiml_engine_window_pos_y", i32, {p}), {w});
            llvm::Value* v = llvm::UndefValue::get(ctx.getLLVMType("ivec2"));
            v = ctx.builder.CreateInsertValue(v, px, 0);
            v = ctx.builder.CreateInsertValue(v, py, 1);
            return ctx.ir_gen->wrap(v);
        };
        eng.builtinFuncs["window_size"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* w = args.empty()
                ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p))
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* sx = ctx.builder.CreateCall(ctx.getFunction("aiml_engine_window_size_x", i32, {p}), {w});
            llvm::Value* sy = ctx.builder.CreateCall(ctx.getFunction("aiml_engine_window_size_y", i32, {p}), {w});
            llvm::Value* v = llvm::UndefValue::get(ctx.getLLVMType("ivec2"));
            v = ctx.builder.CreateInsertValue(v, sx, 0);
            v = ctx.builder.CreateInsertValue(v, sy, 1);
            return ctx.ir_gen->wrap(v);
        };
        // spec/43_5 §8
        eng.builtinFuncs["framebuffer_size"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* w = args.empty()
                ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p))
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* sx = ctx.builder.CreateCall(ctx.getFunction("aiml_engine_framebuffer_size_x", i32, {p}), {w});
            llvm::Value* sy = ctx.builder.CreateCall(ctx.getFunction("aiml_engine_framebuffer_size_y", i32, {p}), {w});
            llvm::Value* v = llvm::UndefValue::get(ctx.getLLVMType("ivec2"));
            v = ctx.builder.CreateInsertValue(v, sx, 0);
            v = ctx.builder.CreateInsertValue(v, sy, 1);
            return ctx.ir_gen->wrap(v);
        };
        eng.builtinFuncs["wait_events"]       = gl43Fn("aiml_engine_wait_events", "pi", 'i');
        eng.builtinFuncs["reset_frame_clock"] = gl43Fn("aiml_engine_reset_frame_clock", "p", 'v');
        eng.builtinFuncs["window_size_x"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* w = args.empty()
                ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p))
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_engine_window_size_x", ctx.builder.getInt32Ty(), {p}), {w}));
        };
        eng.builtinFuncs["window_size_y"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* w = args.empty()
                ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p))
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_engine_window_size_y", ctx.builder.getInt32Ty(), {p}), {w}));
        };
        // engine.window_size_xf / _yf — same as above but cast to f32. Useful
        // when the value participates in float math (scaling, ratios). AIML
        // doesn't auto-promote i32→f32 in arithmetic, so a dedicated f32
        // accessor is the cleanest workaround.
        eng.builtinFuncs["window_size_xf"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* w = args.empty()
                ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p))
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* iv = ctx.builder.CreateCall(
                ctx.getFunction("aiml_engine_window_size_x", ctx.builder.getInt32Ty(), {p}), {w});
            return ctx.ir_gen->wrap(ctx.builder.CreateSIToFP(iv, ctx.builder.getFloatTy()));
        };
        eng.builtinFuncs["window_size_yf"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* w = args.empty()
                ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p))
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* iv = ctx.builder.CreateCall(
                ctx.getFunction("aiml_engine_window_size_y", ctx.builder.getInt32Ty(), {p}), {w});
            return ctx.ir_gen->wrap(ctx.builder.CreateSIToFP(iv, ctx.builder.getFloatTy()));
        };

        // engine.make_current(win), engine.set_vsync(on)
        eng.builtinFuncs["make_current"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* w = args.empty()
                ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p))
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            ctx.builder.CreateCall(ctx.getFunction("aiml_engine_make_current",
                ctx.builder.getVoidTy(), {p}), {w});
            return nullptr;
        };
        eng.builtinFuncs["set_vsync"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            llvm::Value* on = args.empty() ? ctx.builder.getInt32(0)
                : ctx.builder.CreateZExtOrTrunc(
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                    ctx.builder.getInt32Ty());
            ctx.builder.CreateCall(ctx.getFunction("aiml_engine_set_vsync",
                ctx.builder.getVoidTy(), {ctx.builder.getInt32Ty()}), {on});
            return nullptr;
        };

        // engine.run(win, update_fn, render_fn) — passes fn pointers as i8*
        eng.builtinFuncs["run"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            auto null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            auto g = [&](int i) -> llvm::Value* {
                if ((int)args.size() <= i) return null;
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]));
                return v->getType()->isPointerTy() ? v
                    : ctx.builder.CreateIntToPtr(v, p);
            };
            ctx.builder.CreateCall(ctx.getFunction("aiml_engine_run",
                ctx.builder.getVoidTy(), {p, p, p}), {g(0), g(1), g(2)});
            return nullptr;
        };

        // engine.running(win) → bool(i32)
        eng.builtinFuncs["running"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* w = args.empty()
                ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p))
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_engine_running", ctx.builder.getInt32Ty(), {p}), {w}));
        };

        // engine.is_iconified/is_focused/is_visible(win) → i32
        eng.builtinFuncs["is_iconified"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* w = args.empty()
                ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p))
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_engine_is_iconified", ctx.builder.getInt32Ty(), {p}), {w}));
        };
        eng.builtinFuncs["is_focused"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* w = args.empty()
                ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p))
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_engine_is_focused", ctx.builder.getInt32Ty(), {p}), {w}));
        };
        eng.builtinFuncs["is_visible"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* w = args.empty()
                ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p))
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_engine_is_visible", ctx.builder.getInt32Ty(), {p}), {w}));
        };

        // engine.swap(win), engine.quit(win)
        eng.builtinFuncs["swap"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* w = args.empty()
                ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p))
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            ctx.builder.CreateCall(ctx.getFunction("aiml_engine_swap",
                ctx.builder.getVoidTy(), {p}), {w});
            return nullptr;
        };
        eng.builtinFuncs["quit"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* w = args.empty()
                ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p))
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            ctx.builder.CreateCall(ctx.getFunction("aiml_engine_quit",
                ctx.builder.getVoidTy(), {p}), {w});
            return nullptr;
        };

        // engine.dt() → f32, engine.time() → f64, engine.fps() → f32, engine.frame() → i64
        eng.builtinFuncs["dt"]    = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_engine_dt", ctx.builder.getFloatTy(), {}), {})); };
        eng.builtinFuncs["time"]  = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_engine_time", ctx.builder.getDoubleTy(), {}), {})); };
        eng.builtinFuncs["fps"]   = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_engine_fps", ctx.builder.getFloatTy(), {}), {})); };
        eng.builtinFuncs["frame"] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_engine_frame", ctx.builder.getInt64Ty(), {}), {})); };

        // engine.target_fps(fps: i32)
        eng.builtinFuncs["target_fps"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            llvm::Value* fps = args.empty() ? ctx.builder.getInt32(0)
                : ctx.builder.CreateZExtOrTrunc(
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                    ctx.builder.getInt32Ty());
            ctx.builder.CreateCall(ctx.getFunction("aiml_engine_target_fps",
                ctx.builder.getVoidTy(), {ctx.builder.getInt32Ty()}), {fps});
            return nullptr;
        };

        // engine.platform() → string
        eng.builtinFuncs["platform"] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_engine_platform", i8p, {}), {})); };

        // engine.screen_size_x/y → i32
        eng.builtinFuncs["screen_size_x"] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_engine_screen_size_x", ctx.builder.getInt32Ty(), {}), {})); };
        eng.builtinFuncs["screen_size_y"] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_engine_screen_size_y", ctx.builder.getInt32Ty(), {}), {})); };

        // engine.dpi() → f32, engine.is_mobile/is_web → i32
        eng.builtinFuncs["dpi"] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_engine_dpi", ctx.builder.getFloatTy(), {}), {})); };
        eng.builtinFuncs["is_mobile"] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_engine_is_mobile", ctx.builder.getInt32Ty(), {}), {})); };
        eng.builtinFuncs["is_web"] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_engine_is_web", ctx.builder.getInt32Ty(), {}), {})); };
        // ── Prefab functions on engine namespace ────────────────────────────
        // engine.instantiate(name) → prefab_instance
        eng.builtinFuncs["instantiate"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* name = args.empty()
                ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p))
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_instantiate", i8p, {i8p}), {name}));
        };
        // engine.destroy(inst)
        eng.builtinFuncs["destroy"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* inst = args.empty()
                ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p))
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_destroy",
                ctx.builder.getVoidTy(), {i8p}), {inst});
            return nullptr;
        };
        // engine.is_alive(inst) → i32  (1 = still in the scene, 0 = destroyed/null;
        // safe with a stale handle, does not dereference)
        eng.builtinFuncs["is_alive"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* inst = args.empty()
                ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p))
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_is_alive", i32, {i8p}), {inst}));
        };
        // engine.connect(from, to) → connector  (cable between two instances)
        eng.builtinFuncs["connect"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* from = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* to   = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_connect", i8p, {i8p, i8p}), {from, to}));
        };
        // engine.draw_scene()
        eng.builtinFuncs["draw_scene"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_draw_scene",
                ctx.builder.getVoidTy(), {}), {});
            return nullptr;
        };
        // engine.draw_scene_upto(z) — spec/44_6 §8.1: only z_order < z, the
        // rest stays for the closing draw_scene.
        eng.builtinFuncs["draw_scene_upto"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* i32 = ctx.builder.getInt32Ty();
            if (args.empty()) {
                LOG_ERROR(ctx.fmtLoc() + "engine.draw_scene_upto needs the z it draws up to (spec/44_6 §8.1)");
                return nullptr;
            }
            llvm::Value* z = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            if (z->getType()->isFloatingPointTy()) z = ctx.builder.CreateFPToSI(z, i32);
            else if (z->getType() != i32)          z = ctx.builder.CreateZExtOrTrunc(z, i32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_draw_scene_upto",
                ctx.builder.getVoidTy(), {i32}), {z});
            return nullptr;
        };
        // engine.hit_test(mx, my) → prefab_instance
        eng.builtinFuncs["hit_test"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* f32 = ctx.builder.getFloatTy();
            llvm::Value* mx = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* my = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            if (mx->getType() != f32) mx = ctx.builder.CreateSIToFP(mx, f32);
            if (my->getType() != f32) my = ctx.builder.CreateSIToFP(my, f32);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_hit_test", i8p, {f32, f32}), {mx, my}));
        };
        // engine.input_claimed() → i32, engine.input_owner() → prefab_instance
        // spec/44_6: the raw input layer stays a broadcast (spec/44_0 §2.2);
        // game code that must hold back asks here.
        eng.builtinFuncs["input_claimed"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_input_claimed", ctx.builder.getInt32Ty(), {}), {}));
        };
        eng.builtinFuncs["input_owner"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_input_owner", i8p, {}), {}));
        };
        // engine.get_dragged() → prefab_instance
        eng.builtinFuncs["get_dragged"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_get_dragged", i8p, {}), {}));
        };
        // engine.mouse_design_x() / engine.mouse_design_y() → f32
        // Cursor position in DESIGN coordinates (= pixels / rel_scale).  set_pos et al.
        // work in design coords, input.mouse_pos_* returns world/pixels — this
        // encapsulates the conversion in one place (the runtime knows rel_scale).
        eng.builtinFuncs["mouse_design_x"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            auto* f32 = ctx.builder.getFloatTy();
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_mouse_design_x", f32, {}), {}));
        };
        eng.builtinFuncs["mouse_design_y"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            auto* f32 = ctx.builder.getFloatTy();
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_mouse_design_y", f32, {}), {}));
        };
        // engine.update_hover(mx, my)
        eng.builtinFuncs["update_hover"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* f32 = ctx.builder.getFloatTy();
            llvm::Value* mx = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* my = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            if (mx->getType() != f32) mx = ctx.builder.CreateSIToFP(mx, f32);
            if (my->getType() != f32) my = ctx.builder.CreateSIToFP(my, f32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_update_hover",
                ctx.builder.getVoidTy(), {f32, f32}), {mx, my});
            return nullptr;
        };
        // engine.get_hovered() → prefab_instance
        eng.builtinFuncs["get_hovered"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_get_hovered", i8p, {}), {}));
        };
        // engine.layout_hflow(items: list<prefab_instance>, x, y, gap)
        // engine.layout_vflow(items: list<prefab_instance>, x, y, gap)
        // engine.layout_stack(items: list<prefab_instance>, x, y, dx, dy, z_step)
        // Spec 44_6 §Layout — sibling auto-arrange.  items is passed through as
        // AimlList* (i8*); the runtime reads size/data.
        // Helper: list<prefab_instance> struct → (data_ptr i8*, count i32).
        // Accepts both a struct value (local list variable) and
        // pointer-to-struct (class member list, ref-loaded via `<.X`).
        auto extractListDataCount = [](CompilerContext& ctx, AIML::IRValue* listArg,
                                       llvm::Value*& out_data, llvm::Value*& out_count) {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* listVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(listArg));
            llvm::StructType* listStructTy = ListImpl::getListStructType(ctx);
            llvm::Value* sizePtrOrField;
            llvm::Value* dataPtrOrField;
            if (listVal->getType()->isPointerTy()) {
                // Pointer to AimlList → GEP into the struct in-place
                llvm::Value* sizeGEP = ctx.builder.CreateStructGEP(listStructTy, listVal, 0, "list_size_gep");
                out_count = ctx.builder.CreateLoad(i32, sizeGEP, "list_size");
                llvm::Value* dataGEP = ctx.builder.CreateStructGEP(listStructTy, listVal, 2, "list_data_gep");
                out_data = ctx.builder.CreateLoad(i8p, dataGEP, "list_data");
            } else {
                // Struct value → ExtractValue
                out_count = ctx.builder.CreateExtractValue(listVal, {0});
                if (out_count->getType() != i32) out_count = ctx.builder.CreateZExtOrTrunc(out_count, i32);
                llvm::Value* dataPtr = ctx.builder.CreateExtractValue(listVal, {2});
                if (dataPtr->getType() != i8p) dataPtr = ctx.builder.CreateBitCast(dataPtr, i8p);
                out_data = dataPtr;
            }
        };

        auto makeFlowFn = [extractListDataCount](const char* c_name) {
            return [c_name, extractListDataCount](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                auto* f32 = ctx.builder.getFloatTy();
                auto* i32 = ctx.builder.getInt32Ty();
                llvm::Value* data_v = nullptr;
                llvm::Value* count_v = nullptr;
                extractListDataCount(ctx, args[0], data_v, count_v);
                auto toF32 = [&](AIML::IRValue* a) -> llvm::Value* {
                    llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a));
                    if (v->getType() != f32) v = ctx.builder.CreateSIToFP(v, f32);
                    return v;
                };
                ctx.builder.CreateCall(ctx.getFunction(c_name,
                    ctx.builder.getVoidTy(), {i8p, i32, f32, f32, f32}),
                    {data_v, count_v, toF32(args[1]), toF32(args[2]), toF32(args[3])});
                return nullptr;
            };
        };
        eng.builtinFuncs["layout_hflow"] = makeFlowFn("aiml_engine_layout_hflow");
        eng.builtinFuncs["layout_vflow"] = makeFlowFn("aiml_engine_layout_vflow");
        eng.builtinFuncs["layout_stack"] = [extractListDataCount](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* f32 = ctx.builder.getFloatTy();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* data_v = nullptr;
            llvm::Value* count_v = nullptr;
            extractListDataCount(ctx, args[0], data_v, count_v);
            auto toF32 = [&](AIML::IRValue* a) -> llvm::Value* {
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a));
                if (v->getType() != f32) v = ctx.builder.CreateSIToFP(v, f32);
                return v;
            };
            llvm::Value* zs = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[5]));
            if (zs->getType() != i32) zs = ctx.builder.CreateZExtOrTrunc(zs, i32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_engine_layout_stack",
                ctx.builder.getVoidTy(), {i8p, i32, f32, f32, f32, f32, i32}),
                {data_v, count_v, toF32(args[1]), toF32(args[2]), toF32(args[3]), toF32(args[4]), zs});
            return nullptr;
        };
        // engine.update_drag(mx, my, btn_down, btn_released)
        eng.builtinFuncs["update_drag"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* f32 = ctx.builder.getFloatTy();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* mx = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* my = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            llvm::Value* bd = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]));
            llvm::Value* br = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[3]));
            if (mx->getType() != f32) mx = ctx.builder.CreateSIToFP(mx, f32);
            if (my->getType() != f32) my = ctx.builder.CreateSIToFP(my, f32);
            if (bd->getType() != i32) bd = ctx.builder.CreateZExtOrTrunc(bd, i32);
            if (br->getType() != i32) br = ctx.builder.CreateZExtOrTrunc(br, i32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_update_drag",
                ctx.builder.getVoidTy(), {f32, f32, i32, i32}), {mx, my, bd, br});
            return nullptr;
        };
        // engine.update_drag_gesture(mx, my) -> i32 (spec/44_6)
        // Picks up at the PRESS POINT as soon as the movement threshold is crossed, instead of at
        // this frame's cursor as soon as the button is found pressed.
        // 0 none, 1 candidate, 2 dragging, 3 released, 4 cancelled.
        eng.builtinFuncs["update_drag_gesture"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* f32 = ctx.builder.getFloatTy();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* mx = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* my = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            if (mx->getType() != f32) mx = ctx.builder.CreateSIToFP(mx, f32);
            if (my->getType() != f32) my = ctx.builder.CreateSIToFP(my, f32);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_update_drag_gesture", i32, {f32, f32}),
                {mx, my}));
        };
        // engine.text_width(text) -> f32 (spec/44_6)
        // Width in the prefab font, including the inline marker rules.
        // Makes centring/wrapping and the markers checkable at all.
        eng.builtinFuncs["text_width"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* f32 = ctx.builder.getFloatTy();
            llvm::Value* txt = args.empty()
                ? static_cast<llvm::Value*>(ctx.builder.CreateGlobalStringPtr(""))
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_text_width", f32, {i8p}), {txt}));
        };
        // engine.get_drag_source() -> prefab_instance
        eng.builtinFuncs["get_drag_source"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_get_drag_source", i8p, {}), {}));
        };
        // engine.prefab_scene_init(w, h)
        eng.builtinFuncs["prefab_scene_init"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* w = ctx.builder.CreateZExtOrTrunc(
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])), i32);
            llvm::Value* h = ctx.builder.CreateZExtOrTrunc(
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])), i32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_scene_init",
                ctx.builder.getVoidTy(), {i32, i32}), {w, h});
            return nullptr;
        };
        // engine.prefab_set_font(font)
        // engine.prefab_trace_hits(on) — hit trace (spec/44_6).  Off by
        // default; on for UI tests, so that the resolved target is in the log.
        eng.builtinFuncs["prefab_trace_hits"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            llvm::Value* on = args.empty() ? ctx.builder.getInt32(0)
                : ctx.builder.CreateZExtOrTrunc(
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                    ctx.builder.getInt32Ty());
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_trace_hits",
                ctx.builder.getVoidTy(), {ctx.builder.getInt32Ty()}), {on});
            return nullptr;
        };

        // engine.prefab_last_hit() → string: aufgeloeste Treffer des letzten
        // Drucks, "|"-getrennt ("" = miss).  Grundlage von expect_hit (spec/44_9).
        eng.builtinFuncs["prefab_last_hit"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_last_hit", i8p, {}), {}));
        };

        eng.builtinFuncs["prefab_set_font"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* f = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_set_font",
                ctx.builder.getVoidTy(), {i8p}), {f});
            return nullptr;
        };

        // spec/44_6: register an extra named font; a child selects it with `font:`.
        eng.builtinFuncs["prefab_add_font"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            if (args.size() < 2) return nullptr;
            llvm::Value* nm = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* f  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_add_font",
                ctx.builder.getVoidTy(), {i8p, i8p}), {nm, f});
            return nullptr;
        };
    } // end engine

    // ==========================================================================
    // prefab_instance — instance methods
    // ==========================================================================
    {
        CompilerContext::ClassInfo ci;
        ci.name = "prefab_instance"; ci.isBuiltin = true;
        ci.preLoadBase = true;

        auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        auto* f32 = ctx.builder.getFloatTy();
        auto* i32 = ctx.builder.getInt32Ty();
        auto* vd  = ctx.builder.getVoidTy();

        // set_pos(x, y)
        ci.builtinMethods["set_pos"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* f32 = ctx.builder.getFloatTy();
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* x = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* y = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            if (x->getType() != f32) x = ctx.builder.CreateSIToFP(x, f32);
            if (y->getType() != f32) y = ctx.builder.CreateSIToFP(y, f32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_set_pos",
                ctx.builder.getVoidTy(), {i8p, f32, f32}), {base, x, y});
            return nullptr;
        };

        // set_pos_rel(rx, ry) — relative position (0.0-1.0 of screen)
        ci.builtinMethods["set_pos_rel"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* f32 = ctx.builder.getFloatTy();
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* x = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* y = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            if (x->getType() != f32) x = ctx.builder.CreateSIToFP(x, f32);
            if (y->getType() != f32) y = ctx.builder.CreateSIToFP(y, f32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_set_pos_rel",
                ctx.builder.getVoidTy(), {i8p, f32, f32}), {base, x, y});
            return nullptr;
        };

#define PI_F32_GETTER(aimlName, cName) \
        ci.builtinMethods[aimlName] = [](CompilerContext& ctx, llvm::Value* base, \
            AIML::AST::CallExpr*, auto) -> IRValue* { \
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo(); \
            return ctx.ir_gen->wrap(ctx.builder.CreateCall( \
                ctx.getFunction(cName, ctx.builder.getFloatTy(), {i8p}), {base})); \
        };
#define PI_I32_GETTER(aimlName, cName) \
        ci.builtinMethods[aimlName] = [](CompilerContext& ctx, llvm::Value* base, \
            AIML::AST::CallExpr*, auto) -> IRValue* { \
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo(); \
            return ctx.ir_gen->wrap(ctx.builder.CreateCall( \
                ctx.getFunction(cName, ctx.builder.getInt32Ty(), {i8p}), {base})); \
        };
#define PI_F32_SETTER(aimlName, cName) \
        ci.builtinMethods[aimlName] = [](CompilerContext& ctx, llvm::Value* base, \
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* { \
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo(); \
            auto* f32 = ctx.builder.getFloatTy(); \
            llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get()))); \
            if (v->getType() != f32) v = ctx.builder.CreateSIToFP(v, f32); \
            ctx.builder.CreateCall(ctx.getFunction(cName, \
                ctx.builder.getVoidTy(), {i8p, f32}), {base, v}); \
            return nullptr; \
        };
#define PI_I32_SETTER(aimlName, cName) \
        ci.builtinMethods[aimlName] = [](CompilerContext& ctx, llvm::Value* base, \
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* { \
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo(); \
            auto* i32 = ctx.builder.getInt32Ty(); \
            llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get()))); \
            if (v->getType() != i32) v = ctx.builder.CreateZExtOrTrunc(v, i32); \
            ctx.builder.CreateCall(ctx.getFunction(cName, \
                ctx.builder.getVoidTy(), {i8p, i32}), {base, v}); \
            return nullptr; \
        };

        PI_F32_GETTER("get_x", "aiml_prefab_get_x")
        PI_F32_GETTER("get_y", "aiml_prefab_get_y")
        PI_F32_GETTER("get_scale", "aiml_prefab_get_scale")
        PI_F32_GETTER("get_rotation", "aiml_prefab_get_rotation")
        PI_F32_GETTER("get_alpha", "aiml_prefab_get_alpha")
        PI_F32_GETTER("get_width", "aiml_prefab_get_width")
        PI_F32_GETTER("get_height", "aiml_prefab_get_height")
        PI_I32_GETTER("get_z", "aiml_prefab_get_z")
        PI_I32_GETTER("get_visible", "aiml_prefab_get_visible")

        PI_F32_SETTER("set_scale", "aiml_prefab_set_scale")
        PI_F32_SETTER("set_rotation", "aiml_prefab_set_rotation")
        PI_F32_SETTER("set_alpha", "aiml_prefab_set_alpha")
        PI_I32_SETTER("set_z", "aiml_prefab_set_z")
        PI_I32_SETTER("set_visible", "aiml_prefab_set_visible")
        PI_I32_SETTER("set_draggable", "aiml_prefab_set_draggable")
        // spec/44_6: click-through — skipped by hit_test/update_hover,
        // but stays visible (separates "draws" from "takes input").
        PI_I32_SETTER("set_input_transparent", "aiml_prefab_set_input_transparent")
        PI_I32_GETTER("get_input_transparent", "aiml_prefab_get_input_transparent")
        PI_I32_SETTER("set_input_blocking", "aiml_prefab_set_input_blocking")
        PI_I32_GETTER("get_input_blocking", "aiml_prefab_get_input_blocking")
        // spec/44_6 "Input claim": the claim goes by IDENTITY, not by area.
        // claim_input() returns 0 when somebody else holds the pointer — the
        // claim is refused, not stolen.
        PI_I32_GETTER("claim_input", "aiml_prefab_claim_input")
        PI_I32_GETTER("holds_input", "aiml_prefab_holds_input")
        ci.builtinMethods["release_input"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr*, auto) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_release_input",
                ctx.builder.getVoidTy(), {i8p}), {base});
            return nullptr;
        };
        PI_I32_GETTER("is_hovered", "aiml_prefab_is_hovered")

        // cancel_drag() — restore position to before drag started, clear dragged state
        ci.builtinMethods["cancel_drag"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr*, auto) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_cancel_drag", ctx.builder.getVoidTy(), {i8p}), {base});
            return nullptr;
        };

        // was_clicked(btn) → i32
        ci.builtinMethods["was_clicked"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* btn = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            if (btn->getType() != i32) btn = ctx.builder.CreateZExtOrTrunc(btn, i32);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_was_clicked", i32, {i8p, i32}), {base, btn}));
        };

#undef PI_F32_GETTER
#undef PI_I32_GETTER
#undef PI_F32_SETTER
#undef PI_I32_SETTER

        // dock_x(col, colspan) / dock_y(row, rowspan)
        ci.builtinMethods["dock_x"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* col = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* cs  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            if (col->getType() != i32) col = ctx.builder.CreateZExtOrTrunc(col, i32);
            if (cs->getType()  != i32) cs  = ctx.builder.CreateZExtOrTrunc(cs, i32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_dock_x",
                ctx.builder.getVoidTy(), {i8p, i32, i32}), {base, col, cs});
            return nullptr;
        };
        ci.builtinMethods["dock_y"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* row = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* rs  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            if (row->getType() != i32) row = ctx.builder.CreateZExtOrTrunc(row, i32);
            if (rs->getType()  != i32) rs  = ctx.builder.CreateZExtOrTrunc(rs, i32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_dock_y",
                ctx.builder.getVoidTy(), {i8p, i32, i32}), {base, row, rs});
            return nullptr;
        };

        // set_text(child_name, text)
        ci.builtinMethods["set_text"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* text = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_set_text",
                ctx.builder.getVoidTy(), {i8p, i8p, i8p}), {base, name, text});
            return nullptr;
        };

        // set_texture(child_name, tex)
        ci.builtinMethods["set_texture"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* tex  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_set_texture",
                ctx.builder.getVoidTy(), {i8p, i8p, i8p}), {base, name, tex});
            return nullptr;
        };

        // set_color(child_name, color)
        ci.builtinMethods["set_color"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* name  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* color = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            if (color->getType() != i32) color = ctx.builder.CreateZExtOrTrunc(color, i32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_set_color",
                ctx.builder.getVoidTy(), {i8p, i8p, i32}), {base, name, color});
            return nullptr;
        };

        // spec/44_6 §5g: set_color_runs(child, list<i32>) — Tripel (start, len, rgba).
        ci.builtinMethods["set_color_runs"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p   = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32   = ctx.builder.getInt32Ty();
            auto* ptrTy = llvm::PointerType::get(ctx.context, 0);
            if (call->args.size() < 2) return nullptr;
            llvm::StructType* listTy = ListImpl::getListStructType(ctx);
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* lst  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            if (!lst->getType()->isPointerTy()) {
                llvm::AllocaInst* spill = ctx.createEntryAlloca(lst->getType(), "runs_spill");
                ctx.builder.CreateStore(lst, spill);
                lst = spill;
            }
            llvm::Value* szGep = ctx.builder.CreateStructGEP(listTy, lst, 0, "runs_sz_gep");
            llvm::Value* cnt   = ctx.builder.CreateLoad(i32, szGep, "runs_cnt");
            llvm::Value* dtGep = ctx.builder.CreateStructGEP(listTy, lst, 2, "runs_dt_gep");
            llvm::Value* dat   = ctx.builder.CreateLoad(i8p, dtGep, "runs_dat");
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_set_color_runs",
                ctx.builder.getVoidTy(), {i8p, i8p, ptrTy, i32}), {base, name, dat, cnt});
            return nullptr;
        };
        ci.builtinMethods["clear_color_runs"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            if (call->args.empty()) return nullptr;
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_clear_color_runs",
                ctx.builder.getVoidTy(), {i8p, i8p}), {base, name});
            return nullptr;
        };

        // set_bg_color(child_name, color) — child FILL color (rect bg / SDF).
        ci.builtinMethods["set_bg_color"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* name  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* color = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            if (color->getType() != i32) color = ctx.builder.CreateZExtOrTrunc(color, i32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_set_bg_color",
                ctx.builder.getVoidTy(), {i8p, i8p, i32}), {base, name, color});
            return nullptr;
        };

        // set_border_color(child_name, color) — child border/stroke color.
        ci.builtinMethods["set_border_color"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* name  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* color = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            if (color->getType() != i32) color = ctx.builder.CreateZExtOrTrunc(color, i32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_set_border_color",
                ctx.builder.getVoidTy(), {i8p, i8p, i32}), {base, name, color});
            return nullptr;
        };

        // set_child_visible(child_name, v)
        ci.builtinMethods["set_child_visible"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* v    = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            if (v->getType() != i32) v = ctx.builder.CreateZExtOrTrunc(v, i32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_set_child_visible",
                ctx.builder.getVoidTy(), {i8p, i8p, i32}), {base, name, v});
            return nullptr;
        };

        // set_compact(container_name, on) — toggle compacting layout on a
        // container child: hidden spawned rows take no layout space, visible
        // rows re-pack contiguously.  Also settable as `compact: true` in the
        // prefab definition.
        ci.builtinMethods["set_compact"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* v    = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            if (v->getType() != i32) v = ctx.builder.CreateZExtOrTrunc(v, i32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_container_set_compact",
                ctx.builder.getVoidTy(), {i8p, i8p, i32}), {base, name, v});
            return nullptr;
        };

        // set_child_font_size(child_name, size_px) — override a child's
        // `font_size` at runtime (useful for reusing a prefab as scaled
        // preview where the design-time font would render too large).
        ci.builtinMethods["set_child_font_size"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* f32 = ctx.builder.getFloatTy();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* v    = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            if (v->getType() != f32) v = ctx.builder.CreateSIToFP(v, f32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_set_child_font_size",
                ctx.builder.getVoidTy(), {i8p, i8p, f32}), {base, name, v});
            return nullptr;
        };

        // container_row_count(child_name) → i32 — # spawned rows
        ci.builtinMethods["container_row_count"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* rv = ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_container_row_count", i32, {i8p, i8p}),
                {base, name});
            return ctx.ir_gen->wrap(rv);
        };

        // container_row(child_name, idx) → prefab_instance — the spawned row
        // instance at that index (or null if out of range).  Enables AIML
        // code to iterate rows and call set_text / set_texture etc. on each.
        ci.builtinMethods["container_row"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* idx  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            llvm::Value* rv = ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_container_row", i8p, {i8p, i8p, i32}),
                {base, name, idx});
            return ctx.ir_gen->wrap(rv);
        };

        // set_bar_value(child_name, value) — update a "bar" child's fill
        ci.builtinMethods["set_bar_value"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* f32 = ctx.builder.getFloatTy();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* v    = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            if (v->getType() != f32) v = ctx.builder.CreateSIToFP(v, f32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_set_bar_value",
                ctx.builder.getVoidTy(), {i8p, i8p, f32}), {base, name, v});
            return nullptr;
        };
        // set_bar_max(child_name, max)
        ci.builtinMethods["set_bar_max"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* f32 = ctx.builder.getFloatTy();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* v    = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            if (v->getType() != f32) v = ctx.builder.CreateSIToFP(v, f32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_set_bar_max",
                ctx.builder.getVoidTy(), {i8p, i8p, f32}), {base, name, v});
            return nullptr;
        };
        // set_bar_fill(child_name, color) — recolor a "bar" child's fill
        ci.builtinMethods["set_bar_fill"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* c    = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            if (c->getType() != i32) c = ctx.builder.CreateZExtOrTrunc(c, i32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_set_bar_fill",
                ctx.builder.getVoidTy(), {i8p, i8p, i32}), {base, name, c});
            return nullptr;
        };

        // ── slider child accessors ────────────────────────────────────────
        // get_slider_value(child_name) → f32
        ci.builtinMethods["get_slider_value"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* f32 = ctx.builder.getFloatTy();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_get_slider_value", f32, {i8p, i8p}),
                {base, name}));
        };
        // set_slider_value(child_name, value)
        ci.builtinMethods["set_slider_value"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* f32 = ctx.builder.getFloatTy();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* v    = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            if (v->getType() != f32) v = ctx.builder.CreateSIToFP(v, f32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_set_slider_value",
                ctx.builder.getVoidTy(), {i8p, i8p, f32}), {base, name, v});
            return nullptr;
        };
        // set_slider_min(child_name, min)
        ci.builtinMethods["set_slider_min"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* f32 = ctx.builder.getFloatTy();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* v    = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            if (v->getType() != f32) v = ctx.builder.CreateSIToFP(v, f32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_set_slider_min",
                ctx.builder.getVoidTy(), {i8p, i8p, f32}), {base, name, v});
            return nullptr;
        };
        // set_slider_max(child_name, max)
        ci.builtinMethods["set_slider_max"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* f32 = ctx.builder.getFloatTy();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* v    = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            if (v->getType() != f32) v = ctx.builder.CreateSIToFP(v, f32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_set_slider_max",
                ctx.builder.getVoidTy(), {i8p, i8p, f32}), {base, name, v});
            return nullptr;
        };

        // child(name) → prefab_instance
        ci.builtinMethods["child"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_child", i8p, {i8p, i8p}), {base, name}));
        };

        // text_value(child_name) → string (text content of a text_input child)
        ci.builtinMethods["text_value"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            // spec/61 §4: the runtime returns a fresh dup → owned statement temp (a binding adopts it).
            llvm::Value* own = ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_text_value", i8p, {i8p, i8p}), {base, name});
            if (ctx.lifetimeDropsEnabled && !ctx.stmtTempStack.empty()) AIML::AST::aimlTrackStringTemp(ctx, own);
            return ctx.ir_gen->wrap(own);
        };

        // select_all(child_name) → selects the whole content + focus (spec/44_6 §5f)
        ci.builtinMethods["select_all"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_select_all",
                ctx.builder.getVoidTy(), {i8p, i8p}), {base, name});
            return nullptr;
        };

        // selection(child_name) → string (selected text, "" if nothing is selected)
        ci.builtinMethods["selection"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            // spec/61 §4: the runtime returns a fresh dup → owned statement temp.
            llvm::Value* own = ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_selection", i8p, {i8p, i8p}), {base, name});
            if (ctx.lifetimeDropsEnabled && !ctx.stmtTempStack.empty()) AIML::AST::aimlTrackStringTemp(ctx, own);
            return ctx.ir_gen->wrap(own);
        };

        // child_clicked(child_name) → i32 (1 if button child clicked this frame)
        ci.builtinMethods["child_clicked"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_child_clicked", i32, {i8p, i8p}), {base, name}));
        };

        // child_contains(child_name, screen_x, screen_y) → i32 (1 if the SCREEN
        // point is inside the named child's bounds — works for any child type,
        // incl. non-interactive rects; used for drop-target detection).
        ci.builtinMethods["child_contains"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            auto* f32 = ctx.builder.getFloatTy();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* sx   = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            llvm::Value* sy   = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[2].get())));
            if (sx->getType()->isIntegerTy()) sx = ctx.builder.CreateSIToFP(sx, f32);
            if (sy->getType()->isIntegerTy()) sy = ctx.builder.CreateSIToFP(sy, f32);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_child_contains", i32, {i8p, i8p, f32, f32}), {base, name, sx, sy}));
        };

        // child_rect_x/y/w/h(child_name) → f32: the child's hit rect in SCREEN
        // coordinates (spec/44_6).  The inverse of child_contains — a test driver
        // resolves "button X" to a cursor position with these (spec/44_9).
        // -1 = child does not exist.
#define PREFAB_RECT_METHOD(mName, cName)                                        \
        ci.builtinMethods[mName] = [](CompilerContext& ctx, llvm::Value* base,   \
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {              \
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();                 \
            auto* f32 = ctx.builder.getFloatTy();                                \
            llvm::Value* name = static_cast<llvm::Value*>(                       \
                ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));              \
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(                      \
                ctx.getFunction(cName, f32, {i8p, i8p}), {base, name}));         \
        };
        PREFAB_RECT_METHOD("child_rect_x", "aiml_prefab_child_rect_x")
        PREFAB_RECT_METHOD("child_rect_y", "aiml_prefab_child_rect_y")
        PREFAB_RECT_METHOD("child_rect_w", "aiml_prefab_child_rect_w")
        PREFAB_RECT_METHOD("child_rect_h", "aiml_prefab_child_rect_h")
        // child_design_x/y/w/h(child_name) → f32: the same rectangle in DESIGN
        // coordinates (1280x720).  child_rect_* is SCREEN space; set_pos/x/y
        // expect design space.  Whoever places an instance next to a child
        // (tooltip, speech bubble) needs this variant — otherwise
        // the window scaling shifts the result.
        PREFAB_RECT_METHOD("child_design_x", "aiml_prefab_child_design_x")
        PREFAB_RECT_METHOD("child_design_y", "aiml_prefab_child_design_y")
        PREFAB_RECT_METHOD("child_design_w", "aiml_prefab_child_design_w")
        PREFAB_RECT_METHOD("child_design_h", "aiml_prefab_child_design_h")
#undef PREFAB_RECT_METHOD

        // child_focused(child_name) → i32 (1 if text_input child has focus)
        ci.builtinMethods["child_focused"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_child_focused", i32, {i8p, i8p}), {base, name}));
        };

        // is_checked(child_name) → i32 (1 if checkbox child is toggled on)
        ci.builtinMethods["is_checked"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_child_checked", i32, {i8p, i8p}), {base, name}));
        };

        // set_checked(child_name, checked) — set initial / programmatic toggle state
        ci.builtinMethods["set_checked"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* v    = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            if (v->getType() != i32) v = ctx.builder.CreateZExtOrTrunc(v, i32);
            ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_set_checked", ctx.builder.getVoidTy(), {i8p, i8p, i32}),
                {base, name, v});
            return nullptr;
        };

        // dropdown_index(child_name) → i32 — current selected index
        ci.builtinMethods["dropdown_index"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_dropdown_index", i32, {i8p, i8p}), {base, name}));
        };

        // dropdown_value(child_name) → string — current selected option label
        ci.builtinMethods["dropdown_value"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            // spec/61 §4: the runtime returns a fresh dup → owned statement temp (a binding adopts it).
            llvm::Value* own = ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_dropdown_value", i8p, {i8p, i8p}), {base, name});
            if (ctx.lifetimeDropsEnabled && !ctx.stmtTempStack.empty()) AIML::AST::aimlTrackStringTemp(ctx, own);
            return ctx.ir_gen->wrap(own);
        };

        // dropdown_enum(child_name) → i32 — selected option's real enum member
        // value (for a dropdown declared with `enum: TypeName`).  i32 is enum-
        // compatible, so the result compares/assigns directly against the enum.
        ci.builtinMethods["dropdown_enum"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_dropdown_enum_value", i32, {i8p, i8p}), {base, name}));
        };

        // set_dropdown_options(child_name, options) — replace pipe-separated options list
        ci.builtinMethods["set_dropdown_options"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* opts = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_set_dropdown_options", ctx.builder.getVoidTy(), {i8p, i8p, i8p}),
                {base, name, opts});
            return nullptr;
        };

        // set_dropdown_index(child_name, index) — set selected index
        ci.builtinMethods["set_dropdown_index"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* v    = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            if (v->getType() != i32) v = ctx.builder.CreateZExtOrTrunc(v, i32);
            ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_set_dropdown_index", ctx.builder.getVoidTy(), {i8p, i8p, i32}),
                {base, name, v});
            return nullptr;
        };

        // set_dropdown_selected(child_name, value) — find value in the dropdown's
        // pipe-separated options and set that index.  No-op if value not found.
        ci.builtinMethods["set_dropdown_selected"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* val  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_set_dropdown_selected", ctx.builder.getVoidTy(), {i8p, i8p, i8p}),
                {base, name, val});
            return nullptr;
        };

        // Phase 6: bind_list(list_ptr) — attach the raw list<list<i32>> struct
        // that CHILD_CONTAINER widgets iterate over.  The argument is the
        // AIML list value itself (or `list_field` on a model); we just take
        // its address as the struct pointer.
        // ── Widget↔model binding (spec 44.6 §5d) ──────────────────────────
        // bind_model(m) attaches a model instance; sync_from/to_model move each
        // bound child's field to/from its widget.  Offsets/tags are baked into
        // the prefab at compile time (see ast_compiler prefab emission).
        ci.builtinMethods["bind_model"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            if (call->args.empty()) return nullptr;
            llvm::Value* modelPtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            if (modelPtr->getType() != i8p) modelPtr = ctx.builder.CreateBitCast(modelPtr, i8p);
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_bind_model",
                ctx.builder.getVoidTy(), {i8p, i8p}), {base, modelPtr});
            return nullptr;
        };
        ci.builtinMethods["sync_from_model"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* /*call*/, auto /*evalExpr*/) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_sync_from_model",
                ctx.builder.getVoidTy(), {i8p}), {base});
            return nullptr;
        };
        ci.builtinMethods["sync_to_model"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* /*call*/, auto /*evalExpr*/) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_sync_to_model",
                ctx.builder.getVoidTy(), {i8p}), {base});
            return nullptr;
        };
        ci.builtinMethods["bind_list"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            /* Two forms:
             *   scene.bind_list(list)                        (instance-level)
             *   scene.bind_list("child_name", list)          (per-widget)   */
            if (call->args.size() >= 2) {
                llvm::Value* childNm = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
                llvm::Value* listPtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
                ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_child_bind_list",
                    ctx.builder.getVoidTy(), {i8p, i8p, i8p}), {base, childNm, listPtr});
            } else {
                llvm::Value* listPtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
                ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_bind_list",
                    ctx.builder.getVoidTy(), {i8p, i8p}), {base, listPtr});
            }
            return nullptr;
        };
        // bound_list() — inverse of bind_list; returns the attached pointer or null.
        ci.builtinMethods["bound_list"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* /*call*/, auto /*evalExpr*/) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_bound_list", i8p, {i8p}), {base}));
        };
        // bound_index() — for spawned children: index into the parent's bound_list.
        ci.builtinMethods["bound_index"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* /*call*/, auto /*evalExpr*/) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_bound_index", ctx.builder.getInt32Ty(), {i8p}),
                {base}));
        };

        // add_child(other) — nest another instance under this one (inherits transform)
        ci.builtinMethods["add_child"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* child = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_add_child",
                ctx.builder.getVoidTy(), {i8p, i8p}), {base, child});
            return nullptr;
        };
        // remove_child(other) — detach a nested instance (back to top-level)
        ci.builtinMethods["remove_child"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* child = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_remove_child",
                ctx.builder.getVoidTy(), {i8p, i8p}), {base, child});
            return nullptr;
        };

        // set_clip(enabled) — clip nested children to this instance's bounds
        ci.builtinMethods["set_clip"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            if (v->getType() != i32) v = ctx.builder.CreateZExtOrTrunc(v, i32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_set_clip",
                ctx.builder.getVoidTy(), {i8p, i32}), {base, v});
            return nullptr;
        };
        // set_scroll(sx, sy) — scroll offset (local px) for nested children
        ci.builtinMethods["set_scroll"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* f32 = ctx.builder.getFloatTy();
            llvm::Value* x = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* y = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            if (x->getType() != f32) x = ctx.builder.CreateSIToFP(x, f32);
            if (y->getType() != f32) y = ctx.builder.CreateSIToFP(y, f32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_set_scroll",
                ctx.builder.getVoidTy(), {i8p, f32, f32}), {base, x, y});
            return nullptr;
        };
        // focus(child) — give keyboard focus to a text_input child.
        ci.builtinMethods["focus"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_set_focus",
                ctx.builder.getVoidTy(), {i8p, i8p}), {base, name});
            return nullptr;
        };
        // scroll_to_end(child) — pin a textbox / scrollable container to the bottom.
        ci.builtinMethods["scroll_to_end"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* name = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_scroll_to_end",
                ctx.builder.getVoidTy(), {i8p, i8p}), {base, name});
            return nullptr;
        };
        ci.builtinMethods["get_scroll_x"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr*, auto) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_get_scroll_x", ctx.builder.getFloatTy(), {i8p}), {base}));
        };
        ci.builtinMethods["get_scroll_y"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr*, auto) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_prefab_get_scroll_y", ctx.builder.getFloatTy(), {i8p}), {base}));
        };

        // set_connector_anchor(mode) — 0=center, 1=edge, 2=distributed.
        // Controls how cables (see engine.connect) attach to this instance.
        ci.builtinMethods["set_connector_anchor"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* m = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            if (m->getType() != i32) m = ctx.builder.CreateZExtOrTrunc(m, i32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_prefab_set_connector_anchor",
                ctx.builder.getVoidTy(), {i8p, i32}), {base, m});
            return nullptr;
        };

        ctx.classes["prefab_instance"] = std::move(ci);
    } // end prefab_instance

    // ==========================================================================
    // connector — cable between two prefab instances (see engine.connect)
    // ==========================================================================
    {
        CompilerContext::ClassInfo ci;
        ci.name = "connector"; ci.isBuiltin = true;
        ci.preLoadBase = true;

        // set_colors(c0, c1) — gradient start/end (0xRRGGBBAA)
        ci.builtinMethods["set_colors"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* a = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* b = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            if (a->getType() != i32) a = ctx.builder.CreateZExtOrTrunc(a, i32);
            if (b->getType() != i32) b = ctx.builder.CreateZExtOrTrunc(b, i32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_connector_set_colors",
                ctx.builder.getVoidTy(), {i8p, i32, i32}), {base, a, b});
            return nullptr;
        };
        // set_highlight(color) — specular streak tint
        ci.builtinMethods["set_highlight"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* a = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            if (a->getType() != i32) a = ctx.builder.CreateZExtOrTrunc(a, i32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_connector_set_highlight",
                ctx.builder.getVoidTy(), {i8p, i32}), {base, a});
            return nullptr;
        };
        // set_width(w) — cable thickness in px
        ci.builtinMethods["set_width"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* f32 = ctx.builder.getFloatTy();
            llvm::Value* w = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            if (w->getType() != f32) w = ctx.builder.CreateSIToFP(w, f32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_connector_set_width",
                ctx.builder.getVoidTy(), {i8p, f32}), {base, w});
            return nullptr;
        };
        // set_bow(amount) — perpendicular curve (0 = straight). Fraction of chord length.
        ci.builtinMethods["set_bow"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* f32 = ctx.builder.getFloatTy();
            llvm::Value* w = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            if (w->getType() != f32) w = ctx.builder.CreateSIToFP(w, f32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_connector_set_bow",
                ctx.builder.getVoidTy(), {i8p, f32}), {base, w});
            return nullptr;
        };
        // set_visible(v)
        ci.builtinMethods["set_visible"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            if (v->getType() != i32) v = ctx.builder.CreateZExtOrTrunc(v, i32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_connector_set_visible",
                ctx.builder.getVoidTy(), {i8p, i32}), {base, v});
            return nullptr;
        };
        // set_z(z) — z-order for interleaving with prefabs in draw_scene
        ci.builtinMethods["set_z"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* z = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            if (z->getType() != i32) z = ctx.builder.CreateZExtOrTrunc(z, i32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_connector_set_z",
                ctx.builder.getVoidTy(), {i8p, i32}), {base, z});
            return nullptr;
        };
        // set_port(tex, scale) — optional endpoint sprite; scale = size / width
        ci.builtinMethods["set_port"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* f32 = ctx.builder.getFloatTy();
            llvm::Value* tex = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* sc  = call->args.size() > 1
                ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())))
                : llvm::ConstantFP::get(f32, 1.6);
            if (sc->getType() != f32) sc = ctx.builder.CreateSIToFP(sc, f32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_connector_set_port",
                ctx.builder.getVoidTy(), {i8p, i8p, f32}), {base, tex, sc});
            return nullptr;
        };
        // destroy()
        ci.builtinMethods["destroy"] = [](CompilerContext& ctx, llvm::Value* base,
            AIML::AST::CallExpr*, auto) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            ctx.builder.CreateCall(ctx.getFunction("aiml_connector_destroy",
                ctx.builder.getVoidTy(), {i8p}), {base});
            return nullptr;
        };

        ctx.classes["connector"] = std::move(ci);
    } // end connector

    // ==========================================================================
    // input — keyboard / mouse / gamepad / event-queue / mapping layer
    // ==========================================================================
    {
        // ── input_event type ─────────────────────────────────────────────────
        {
            CompilerContext::ClassInfo ci;
            ci.name = "input_event"; ci.isBuiltin = true; ci.preLoadBase = true;
            // Property getters (zero-arg, accessed via resolveProperty → builtinMethods)
#define INP_STR_GETTER(aimlName, cName) \
            ci.builtinMethods[aimlName] = [](CompilerContext& ctx, llvm::Value* base, \
                AIML::AST::CallExpr*, auto) -> IRValue* { \
                auto* i8p = ctx.builder.getInt8Ty()->getPointerTo(); \
                return ctx.ir_gen->wrap(ctx.builder.CreateCall( \
                    ctx.getFunction(cName, i8p, {i8p}), {base})); \
            };
#define INP_I32_GETTER(aimlName, cName) \
            ci.builtinMethods[aimlName] = [](CompilerContext& ctx, llvm::Value* base, \
                AIML::AST::CallExpr*, auto) -> IRValue* { \
                auto* i8p = ctx.builder.getInt8Ty()->getPointerTo(); \
                return ctx.ir_gen->wrap(ctx.builder.CreateCall( \
                    ctx.getFunction(cName, ctx.builder.getInt32Ty(), {i8p}), {base})); \
            };
#define INP_F32_GETTER(aimlName, cName) \
            ci.builtinMethods[aimlName] = [](CompilerContext& ctx, llvm::Value* base, \
                AIML::AST::CallExpr*, auto) -> IRValue* { \
                auto* i8p = ctx.builder.getInt8Ty()->getPointerTo(); \
                return ctx.ir_gen->wrap(ctx.builder.CreateCall( \
                    ctx.getFunction(cName, ctx.builder.getFloatTy(), {i8p}), {base})); \
            };
            INP_STR_GETTER("type",     "aiml_input_event_type")
            INP_STR_GETTER("key",      "aiml_input_event_key")
            INP_I32_GETTER("btn",      "aiml_input_event_btn")
            INP_F32_GETTER("pos_x",    "aiml_input_event_pos_x")
            INP_F32_GETTER("pos_y",    "aiml_input_event_pos_y")
            INP_F32_GETTER("scroll_x", "aiml_input_event_scroll_x")
            INP_F32_GETTER("scroll_y", "aiml_input_event_scroll_y")
            INP_STR_GETTER("char_",    "aiml_input_event_char_")
            INP_F32_GETTER("delta_x",  "aiml_input_event_delta_x")
            INP_F32_GETTER("delta_y",  "aiml_input_event_delta_y")
#undef INP_STR_GETTER
#undef INP_I32_GETTER
#undef INP_F32_GETTER
            ctx.classes["input_event"] = std::move(ci);
        }

        auto& inp = ctx.namespaces["input"];
        auto* p   = llvm::PointerType::get(ctx.context, 0);
        auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        auto* i32 = ctx.builder.getInt32Ty();
        auto* f32 = ctx.builder.getFloatTy();
        auto* vd  = ctx.builder.getVoidTy();
        auto null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));

        // Helper: get string arg i (i8*)
        // Helper: get i32 arg i
        // Helper: get f32 arg i

        // input.init(win)
        inp.builtinFuncs["init"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto* vd  = ctx.builder.getVoidTy();
            llvm::Value* w = args.empty()
                ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p))
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            ctx.builder.CreateCall(ctx.getFunction("aiml_input_init", vd, {p}), {w});
            return nullptr;
        };

        // input.update() / input.set_capture(on)
        inp.builtinFuncs["update"] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            ctx.builder.CreateCall(ctx.getFunction("aiml_input_update", ctx.builder.getVoidTy(), {}), {});
            return nullptr;
        };
        inp.builtinFuncs["set_capture"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            llvm::Value* on = args.empty() ? ctx.builder.getInt32(0)
                : ctx.builder.CreateZExtOrTrunc(
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                    ctx.builder.getInt32Ty());
            ctx.builder.CreateCall(ctx.getFunction("aiml_input_set_capture",
                ctx.builder.getVoidTy(), {ctx.builder.getInt32Ty()}), {on});
            return nullptr;
        };

        // input.clipboard_set(s: string) / input.clipboard_get() → string
        // System clipboard (nativ GLFW, Browser navigator.clipboard + Fallback).
        inp.builtinFuncs["clipboard_set"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* s = args.empty()
                ? static_cast<llvm::Value*>(ctx.builder.CreateGlobalStringPtr(""))
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            ctx.builder.CreateCall(ctx.getFunction("aiml_input_clipboard_set",
                ctx.builder.getVoidTy(), {i8p}), {s});
            return nullptr;
        };
        inp.builtinFuncs["clipboard_get"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_input_clipboard_get", i8p, {}), {}));
        };

        // input.post_* — input injection (spec/44_0 §8).  Goes through the same
        // callbacks as a real event; a test driver needs it to operate a
        // UI (spec/44_9).
        inp.builtinFuncs["post_mouse_move"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* f32 = ctx.builder.getFloatTy();
            auto num = [&](size_t i) -> llvm::Value* {
                if (args.size() <= i) return llvm::ConstantFP::get(f32, 0.0);
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]));
                if (v->getType()->isIntegerTy()) v = ctx.builder.CreateSIToFP(v, f32);
                if (v->getType()->isDoubleTy())  v = ctx.builder.CreateFPTrunc(v, f32);
                return v;
            };
            ctx.builder.CreateCall(ctx.getFunction("aiml_input_post_mouse_move",
                ctx.builder.getVoidTy(), {f32, f32}), {num(0), num(1)});
            return nullptr;
        };
        /* spec/44_0 §8.4: the same signature, but into the EVENT layer. */
        inp.builtinFuncs["post_mouse_move_pending"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* f32 = ctx.builder.getFloatTy();
            auto num = [&](size_t i) -> llvm::Value* {
                if (args.size() <= i) return llvm::ConstantFP::get(f32, 0.0);
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]));
                if (v->getType()->isIntegerTy()) v = ctx.builder.CreateSIToFP(v, f32);
                if (v->getType()->isDoubleTy())  v = ctx.builder.CreateFPTrunc(v, f32);
                return v;
            };
            ctx.builder.CreateCall(ctx.getFunction("aiml_input_post_mouse_move_pending",
                ctx.builder.getVoidTy(), {f32, f32}), {num(0), num(1)});
            return nullptr;
        };
        inp.builtinFuncs["post_scroll"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* f32 = ctx.builder.getFloatTy();
            auto num = [&](size_t i) -> llvm::Value* {
                if (args.size() <= i) return llvm::ConstantFP::get(f32, 0.0);
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]));
                if (v->getType()->isIntegerTy()) v = ctx.builder.CreateSIToFP(v, f32);
                if (v->getType()->isDoubleTy())  v = ctx.builder.CreateFPTrunc(v, f32);
                return v;
            };
            ctx.builder.CreateCall(ctx.getFunction("aiml_input_post_scroll",
                ctx.builder.getVoidTy(), {f32, f32}), {num(0), num(1)});
            return nullptr;
        };
#define INP_POST_BTN(name, cName) \
        inp.builtinFuncs[name] = [](CompilerContext& ctx, \
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* { \
            llvm::Value* b = args.empty() ? ctx.builder.getInt32(0) \
                : ctx.builder.CreateZExtOrTrunc( \
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])), \
                    ctx.builder.getInt32Ty()); \
            ctx.builder.CreateCall(ctx.getFunction(cName, \
                ctx.builder.getVoidTy(), {ctx.builder.getInt32Ty()}), {b}); \
            return nullptr; \
        };
        INP_POST_BTN("post_mouse_down", "aiml_input_post_mouse_down")
        INP_POST_BTN("post_mouse_up",   "aiml_input_post_mouse_up")
        INP_POST_BTN("post_mouse_down_pending", "aiml_input_post_mouse_down_pending")
        INP_POST_BTN("post_mouse_up_pending",   "aiml_input_post_mouse_up_pending")
#undef INP_POST_BTN
        // post_focus_lost() -> void — spec/44_0 §8.2: the window lost focus.
        // Cancels every active press AND releases every held key (§2.1), so a
        // scenario can prove that no key stays stuck down without opening a
        // second window.
        inp.builtinFuncs["post_focus_lost"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            ctx.builder.CreateCall(ctx.getFunction("aiml_input_post_focus_lost",
                ctx.builder.getVoidTy(), {}), {});
            return nullptr;
        };
        // post_touch_cancel() -> void — spec/44_0 §8.2: abort the active press
        // WITHOUT a release latch, so a scenario can prove that an aborted
        // gesture produces no drop.
        inp.builtinFuncs["post_touch_cancel"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            ctx.builder.CreateCall(ctx.getFunction("aiml_input_post_touch_cancel",
                ctx.builder.getVoidTy(), {}), {});
            return nullptr;
        };
#define INP_POST_STR(name, cName) \
        inp.builtinFuncs[name] = [](CompilerContext& ctx, \
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* { \
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo(); \
            llvm::Value* s = args.empty() \
                ? static_cast<llvm::Value*>(ctx.builder.CreateGlobalStringPtr("")) \
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])); \
            ctx.builder.CreateCall(ctx.getFunction(cName, \
                ctx.builder.getVoidTy(), {i8p}), {s}); \
            return nullptr; \
        };
        INP_POST_STR("post_key_down", "aiml_input_post_key_down")
        INP_POST_STR("post_key_up",   "aiml_input_post_key_up")
        INP_POST_STR("post_text",     "aiml_input_post_text")
#undef INP_POST_STR

        // input.os_* — OS-level injection via XTEST (spec/44_0 §8.5).
        // Coordinates are SCREEN coordinates, not window pixels.
        inp.builtinFuncs["os_available"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_os_input_available", ctx.builder.getInt32Ty(), {}), {}));
        };
        inp.builtinFuncs["os_mouse_move"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* i32 = ctx.builder.getInt32Ty();
            auto num = [&](size_t i) -> llvm::Value* {
                if (args.size() <= i) return ctx.builder.getInt32(0);
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]));
                if (v->getType()->isFloatingPointTy()) return ctx.builder.CreateFPToSI(v, i32);
                return ctx.builder.CreateSExtOrTrunc(v, i32);
            };
            ctx.builder.CreateCall(ctx.getFunction("aiml_os_mouse_move",
                ctx.builder.getVoidTy(), {i32, i32}), {num(0), num(1)});
            return nullptr;
        };
#define INP_OS_BTN(name, cName) \
        inp.builtinFuncs[name] = [](CompilerContext& ctx, \
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* { \
            llvm::Value* b = args.empty() ? ctx.builder.getInt32(0) \
                : ctx.builder.CreateZExtOrTrunc( \
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])), \
                    ctx.builder.getInt32Ty()); \
            ctx.builder.CreateCall(ctx.getFunction(cName, \
                ctx.builder.getVoidTy(), {ctx.builder.getInt32Ty()}), {b}); \
            return nullptr; \
        };
        INP_OS_BTN("os_mouse_down", "aiml_os_mouse_down")
        INP_OS_BTN("os_mouse_up",   "aiml_os_mouse_up")
#undef INP_OS_BTN
#define INP_OS_STR(name, cName) \
        inp.builtinFuncs[name] = [](CompilerContext& ctx, \
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* { \
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo(); \
            llvm::Value* str = args.empty() \
                ? static_cast<llvm::Value*>(ctx.builder.CreateGlobalStringPtr("")) \
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])); \
            ctx.builder.CreateCall(ctx.getFunction(cName, \
                ctx.builder.getVoidTy(), {i8p}), {str}); \
            return nullptr; \
        };
        INP_OS_STR("os_key_down", "aiml_os_key_down")
        INP_OS_STR("os_key_up",   "aiml_os_key_up")
#undef INP_OS_STR
#define INP_OS_I32_0(name, cName) \
        inp.builtinFuncs[name] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>&) -> AIML::IRValue* { \
            return ctx.ir_gen->wrap(ctx.builder.CreateCall( \
                ctx.getFunction(cName, ctx.builder.getInt32Ty(), {}), {})); \
        };
        INP_OS_I32_0("os_pointer_x", "aiml_os_pointer_x")
        INP_OS_I32_0("os_pointer_y", "aiml_os_pointer_y")
#undef INP_OS_I32_0

        // input.key_down/pressed/released(key: string) → i32
#define INP_KEY_FUNC(name, cName) \
        inp.builtinFuncs[name] = [](CompilerContext& ctx, \
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* { \
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo(); \
            llvm::Value* k = args.empty() \
                ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p)) \
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])); \
            return ctx.ir_gen->wrap(ctx.builder.CreateCall( \
                ctx.getFunction(cName, ctx.builder.getInt32Ty(), {i8p}), {k})); \
        };
        INP_KEY_FUNC("key_down",        "aiml_input_key_down")
        INP_KEY_FUNC("key_pressed",     "aiml_input_key_pressed")
        INP_KEY_FUNC("key_pressed_once","aiml_input_key_pressed_once")
        INP_KEY_FUNC("key_released",    "aiml_input_key_released")
#undef INP_KEY_FUNC

        // input.mouse_pos_x/y / delta_x/y / scroll_x/y → f32
#define INP_F32_0(name, cName) \
        inp.builtinFuncs[name] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>&) -> AIML::IRValue* { \
            return ctx.ir_gen->wrap(ctx.builder.CreateCall( \
                ctx.getFunction(cName, ctx.builder.getFloatTy(), {}), {})); \
        };
        INP_F32_0("mouse_pos_x",    "aiml_input_mouse_pos_x")
        INP_F32_0("mouse_pos_y",    "aiml_input_mouse_pos_y")
        INP_F32_0("mouse_delta_x",  "aiml_input_mouse_delta_x")
        INP_F32_0("mouse_delta_y",  "aiml_input_mouse_delta_y")
        INP_F32_0("mouse_scroll_x", "aiml_input_mouse_scroll_x")
        INP_F32_0("mouse_scroll_y", "aiml_input_mouse_scroll_y")
#undef INP_F32_0

        // input.mouse_pos() / mouse_delta() / mouse_scroll() → vec2
        inp.builtinFuncs["mouse_pos"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            auto* vec2Ty = static_cast<llvm::StructType*>(ctx.getLLVMType("vec2"));
            llvm::Value* x = ctx.builder.CreateCall(
                ctx.getFunction("aiml_input_mouse_pos_x", ctx.builder.getFloatTy(), {}), {});
            llvm::Value* y = ctx.builder.CreateCall(
                ctx.getFunction("aiml_input_mouse_pos_y", ctx.builder.getFloatTy(), {}), {});
            llvm::Value* v = llvm::UndefValue::get(vec2Ty);
            v = ctx.builder.CreateInsertValue(v, x, 0);
            v = ctx.builder.CreateInsertValue(v, y, 1);
            return ctx.ir_gen->wrap(v);
        };
        inp.builtinFuncs["mouse_delta"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            auto* vec2Ty = static_cast<llvm::StructType*>(ctx.getLLVMType("vec2"));
            llvm::Value* x = ctx.builder.CreateCall(
                ctx.getFunction("aiml_input_mouse_delta_x", ctx.builder.getFloatTy(), {}), {});
            llvm::Value* y = ctx.builder.CreateCall(
                ctx.getFunction("aiml_input_mouse_delta_y", ctx.builder.getFloatTy(), {}), {});
            llvm::Value* v = llvm::UndefValue::get(vec2Ty);
            v = ctx.builder.CreateInsertValue(v, x, 0);
            v = ctx.builder.CreateInsertValue(v, y, 1);
            return ctx.ir_gen->wrap(v);
        };
        inp.builtinFuncs["mouse_scroll"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            auto* vec2Ty = static_cast<llvm::StructType*>(ctx.getLLVMType("vec2"));
            llvm::Value* x = ctx.builder.CreateCall(
                ctx.getFunction("aiml_input_mouse_scroll_x", ctx.builder.getFloatTy(), {}), {});
            llvm::Value* y = ctx.builder.CreateCall(
                ctx.getFunction("aiml_input_mouse_scroll_y", ctx.builder.getFloatTy(), {}), {});
            llvm::Value* v = llvm::UndefValue::get(vec2Ty);
            v = ctx.builder.CreateInsertValue(v, x, 0);
            v = ctx.builder.CreateInsertValue(v, y, 1);
            return ctx.ir_gen->wrap(v);
        };

        // input.mouse_button/pressed/released(btn: i32) → i32
#define INP_BTN_FUNC(name, cName) \
        inp.builtinFuncs[name] = [](CompilerContext& ctx, \
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* { \
            llvm::Value* b = args.empty() ? ctx.builder.getInt32(0) \
                : ctx.builder.CreateZExtOrTrunc( \
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])), \
                    ctx.builder.getInt32Ty()); \
            return ctx.ir_gen->wrap(ctx.builder.CreateCall( \
                ctx.getFunction(cName, ctx.builder.getInt32Ty(), \
                    {ctx.builder.getInt32Ty()}), {b})); \
        };
        INP_BTN_FUNC("mouse_button",   "aiml_input_mouse_button")
        INP_BTN_FUNC("mouse_pressed",  "aiml_input_mouse_pressed")
        INP_BTN_FUNC("mouse_released", "aiml_input_mouse_released")
        INP_BTN_FUNC("mouse_cancelled","aiml_input_mouse_cancelled")
#undef INP_BTN_FUNC

        // input.gamepad_connected(pad) → i32
        inp.builtinFuncs["gamepad_connected"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* pad = args.empty() ? ctx.builder.getInt32(0)
                : ctx.builder.CreateZExtOrTrunc(
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])), i32);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_input_gamepad_connected", i32, {i32}), {pad}));
        };

        // input.gamepad_axis(pad, axis) → f32
        inp.builtinFuncs["gamepad_axis"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* i32 = ctx.builder.getInt32Ty();
            auto* f32 = ctx.builder.getFloatTy();
            auto g32 = [&](int i) -> llvm::Value* {
                if ((int)args.size() <= i) return ctx.builder.getInt32(0);
                return ctx.builder.CreateZExtOrTrunc(
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])), i32);
            };
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_input_gamepad_axis", f32, {i32, i32}),
                {g32(0), g32(1)}));
        };

        // input.gamepad_button/pressed(pad, btn) → i32
#define INP_GP_FUNC(name, cName) \
        inp.builtinFuncs[name] = [](CompilerContext& ctx, \
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* { \
            auto* i32 = ctx.builder.getInt32Ty(); \
            auto g32 = [&](int i) -> llvm::Value* { \
                if ((int)args.size() <= i) return ctx.builder.getInt32(0); \
                return ctx.builder.CreateZExtOrTrunc( \
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])), i32); \
            }; \
            return ctx.ir_gen->wrap(ctx.builder.CreateCall( \
                ctx.getFunction(cName, i32, {i32, i32}), {g32(0), g32(1)})); \
        };
        INP_GP_FUNC("gamepad_button",  "aiml_input_gamepad_button")
        INP_GP_FUNC("gamepad_pressed", "aiml_input_gamepad_pressed")
#undef INP_GP_FUNC

        // input.has_event() → i32
        inp.builtinFuncs["has_event"] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_input_has_event", ctx.builder.getInt32Ty(), {}), {}));
        };

        // input.next_event() → input_event (returns opaque i8* pointer)
        inp.builtinFuncs["next_event"] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_input_next_event", p, {}), {}));
        };

        // input.clear_events()
        inp.builtinFuncs["clear_events"] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            ctx.builder.CreateCall(ctx.getFunction("aiml_input_clear_events",
                ctx.builder.getVoidTy(), {}), {});
            return nullptr;
        };

        // input.map_axis(name, key, scale)
        inp.builtinFuncs["map_axis"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* f32 = ctx.builder.getFloatTy();
            auto null8 = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p));
            auto gstr = [&](int i) -> llvm::Value* {
                if ((int)args.size() <= i) return null8;
                return static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]));
            };
            llvm::Value* scale = args.size() > 2
                ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]))
                : llvm::ConstantFP::get(f32, 1.0);
            if (!scale->getType()->isFloatTy()) scale = ctx.builder.CreateFPCast(scale, f32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_input_map_axis",
                ctx.builder.getVoidTy(), {i8p, i8p, f32}),
                {gstr(0), gstr(1), scale});
            return nullptr;
        };

        // input.map_axis_mouse(name, comp, scale)
        inp.builtinFuncs["map_axis_mouse"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* f32 = ctx.builder.getFloatTy();
            auto null8 = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p));
            auto gstr = [&](int i) -> llvm::Value* {
                if ((int)args.size() <= i) return null8;
                return static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]));
            };
            llvm::Value* scale = args.size() > 2
                ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]))
                : llvm::ConstantFP::get(f32, 1.0);
            if (!scale->getType()->isFloatTy()) scale = ctx.builder.CreateFPCast(scale, f32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_input_map_axis_mouse",
                ctx.builder.getVoidTy(), {i8p, i8p, f32}),
                {gstr(0), gstr(1), scale});
            return nullptr;
        };

        // input.map_axis_gamepad(name, pad, axis, scale)
        inp.builtinFuncs["map_axis_gamepad"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            auto* f32 = ctx.builder.getFloatTy();
            auto null8 = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p));
            llvm::Value* name = args.size() > 0 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])) : null8;
            auto g32 = [&](int i) -> llvm::Value* {
                if ((int)args.size() <= i) return ctx.builder.getInt32(0);
                return ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])), i32);
            };
            llvm::Value* scale = args.size() > 3
                ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[3]))
                : llvm::ConstantFP::get(f32, 1.0);
            if (!scale->getType()->isFloatTy()) scale = ctx.builder.CreateFPCast(scale, f32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_input_map_axis_gamepad",
                ctx.builder.getVoidTy(), {i8p, i32, i32, f32}),
                {name, g32(1), g32(2), scale});
            return nullptr;
        };

        // input.map_action(name, key)
        inp.builtinFuncs["map_action"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto null8 = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p));
            auto gstr = [&](int i) -> llvm::Value* {
                if ((int)args.size() <= i) return null8;
                return static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]));
            };
            ctx.builder.CreateCall(ctx.getFunction("aiml_input_map_action",
                ctx.builder.getVoidTy(), {i8p, i8p}), {gstr(0), gstr(1)});
            return nullptr;
        };

        // input.map_action_mouse(name, btn)
        inp.builtinFuncs["map_action_mouse"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            auto null8 = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p));
            llvm::Value* name = args.size() > 0 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])) : null8;
            llvm::Value* btn  = args.size() > 1
                ? ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])), i32)
                : ctx.builder.getInt32(0);
            ctx.builder.CreateCall(ctx.getFunction("aiml_input_map_action_mouse",
                ctx.builder.getVoidTy(), {i8p, i32}), {name, btn});
            return nullptr;
        };

        // input.map_action_gamepad(name, pad, btn)
        inp.builtinFuncs["map_action_gamepad"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            auto null8 = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p));
            llvm::Value* name = args.size() > 0 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])) : null8;
            auto g32 = [&](int i) -> llvm::Value* {
                if ((int)args.size() <= i) return ctx.builder.getInt32(0);
                return ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])), i32);
            };
            ctx.builder.CreateCall(ctx.getFunction("aiml_input_map_action_gamepad",
                ctx.builder.getVoidTy(), {i8p, i32, i32}), {name, g32(1), g32(2)});
            return nullptr;
        };

        // input.unmap(name) / input.clear_mappings()
        inp.builtinFuncs["unmap"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* name = args.empty()
                ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p))
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            ctx.builder.CreateCall(ctx.getFunction("aiml_input_unmap",
                ctx.builder.getVoidTy(), {i8p}), {name});
            return nullptr;
        };
        inp.builtinFuncs["clear_mappings"] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            ctx.builder.CreateCall(ctx.getFunction("aiml_input_clear_mappings",
                ctx.builder.getVoidTy(), {}), {});
            return nullptr;
        };

        // input.axis(name) → f32
        inp.builtinFuncs["axis"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Value* name = args.empty()
                ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p))
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_input_axis", ctx.builder.getFloatTy(), {i8p}), {name}));
        };

        // input.action_down/pressed/released(name) → i32
#define INP_ACTION_FUNC(name, cName) \
        inp.builtinFuncs[name] = [](CompilerContext& ctx, \
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* { \
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo(); \
            llvm::Value* n = args.empty() \
                ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p)) \
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])); \
            return ctx.ir_gen->wrap(ctx.builder.CreateCall( \
                ctx.getFunction(cName, ctx.builder.getInt32Ty(), {i8p}), {n})); \
        };
        INP_ACTION_FUNC("action_down",     "aiml_input_action_down")
        INP_ACTION_FUNC("action_pressed",  "aiml_input_action_pressed")
        INP_ACTION_FUNC("action_released", "aiml_input_action_released")
#undef INP_ACTION_FUNC

        // ── Drag-and-drop polling ─────────────────────────────────────────
#define INP_DRAG_I32(name, cName) \
        inp.builtinFuncs[name] = [](CompilerContext& ctx, \
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* { \
            auto* i32 = ctx.builder.getInt32Ty(); \
            llvm::Value* b = args.empty() ? ctx.builder.getInt32(0) \
                : ctx.builder.CreateZExtOrTrunc( \
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])), i32); \
            return ctx.ir_gen->wrap(ctx.builder.CreateCall( \
                ctx.getFunction(cName, i32, {i32}), {b})); \
        };
#define INP_DRAG_F32(name, cName) \
        inp.builtinFuncs[name] = [](CompilerContext& ctx, \
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* { \
            auto* i32 = ctx.builder.getInt32Ty(); \
            auto* f32 = ctx.builder.getFloatTy(); \
            llvm::Value* b = args.empty() ? ctx.builder.getInt32(0) \
                : ctx.builder.CreateZExtOrTrunc( \
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])), i32); \
            return ctx.ir_gen->wrap(ctx.builder.CreateCall( \
                ctx.getFunction(cName, f32, {i32}), {b})); \
        };
        INP_DRAG_I32("drag_active",    "aiml_input_drag_active")
        INP_DRAG_I32("drag_started",   "aiml_input_drag_started")
        INP_DRAG_I32("drag_ended",     "aiml_input_drag_ended")
        INP_DRAG_I32("drag_cancelled", "aiml_input_drag_cancelled")
        INP_DRAG_F32("drag_origin_x",  "aiml_input_drag_origin_x")
        INP_DRAG_F32("drag_origin_y",  "aiml_input_drag_origin_y")
        INP_DRAG_F32("drag_start_x",   "aiml_input_drag_start_x")
        INP_DRAG_F32("drag_start_y",   "aiml_input_drag_start_y")
        INP_DRAG_F32("drag_end_x",     "aiml_input_drag_end_x")
        INP_DRAG_F32("drag_end_y",     "aiml_input_drag_end_y")
        INP_DRAG_F32("drag_delta_x",   "aiml_input_drag_delta_x")
        INP_DRAG_F32("drag_delta_y",   "aiml_input_drag_delta_y")
#undef INP_DRAG_I32
#undef INP_DRAG_F32

        // input.drag_set_payload(data: ptr) / drag_get_payload() → ptr
        inp.builtinFuncs["drag_set_payload"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p  = llvm::PointerType::get(ctx.context, 0);
            auto* vd = ctx.builder.getVoidTy();
            llvm::Value* data = args.empty()
                ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p))
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            ctx.builder.CreateCall(ctx.getFunction("aiml_input_drag_set_payload", vd, {p}), {data});
            return nullptr;
        };
        inp.builtinFuncs["drag_get_payload"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_input_drag_get_payload", p, {}), {}));
        };
        inp.builtinFuncs["drag_set_threshold"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* f32 = ctx.builder.getFloatTy();
            auto* vd  = ctx.builder.getVoidTy();
            llvm::Value* px = args.empty() ? llvm::ConstantFP::get(f32, 5.0)
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            if (!px->getType()->isFloatTy()) px = ctx.builder.CreateFPCast(px, f32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_input_drag_set_threshold", vd, {f32}), {px});
            return nullptr;
        };

        (void)p; (void)i8p; (void)i32; (void)f32; (void)vd; (void)null;
    } // end input

    // ==========================================================================
    // ui — Immediate-Mode UI (Grid-Layout + 2D Batch-Renderer + stb_truetype)
    // ==========================================================================
    {
        auto* p    = llvm::PointerType::get(ctx.context, 0);
        auto* i8p  = llvm::Type::getInt8Ty(ctx.context)->getPointerTo();
        auto* i32  = ctx.builder.getInt32Ty();
        auto* f32  = ctx.builder.getFloatTy();
        auto* vd   = ctx.builder.getVoidTy();
        auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));

        // Helper: get arg[i] as pointer (buffer<T> → store to alloca if struct value)
        // ui_style passed by value → store to alloca → pass alloca ptr
        // This lambda captures ctx+args by ref; used in each handler via inline copy.

        // ── ui_font type ────────────────────────────────────────────────────
        {
            CompilerContext::ClassInfo ci;
            ci.name = "ui_font"; ci.isBuiltin = true; ci.preLoadBase = true;
            ci.type = nullptr;
            ctx.classes["ui_font"] = std::move(ci);
        }

        // ── ui_style type ────────────────────────────────────────────────────
        {
            CompilerContext::ClassInfo ci;
            ci.name = "ui_style"; ci.isBuiltin = false;
            ci.type = llvm::cast<llvm::StructType>(ctx.getLLVMType("ui_style"));
            // GEP indices matching AimlUIStyle C struct field order:
            auto ui_sm = [&](int idx, const char* tname) {
                CompilerContext::StructMember m;
                m.index    = idx;
                m.typeName = tname;
                m.access   = AccessLevel::Public;
                return m;
            };
            ci.members["color"]         = ui_sm(0,  "i32");
            ci.members["bg_color"]      = ui_sm(1,  "i32");
            ci.members["hover_color"]   = ui_sm(2,  "i32");
            ci.members["active_color"]  = ui_sm(3,  "i32");
            ci.members["border_color"]  = ui_sm(4,  "i32");
            ci.members["font_size"]     = ui_sm(5,  "f32");
            ci.members["border_width"]  = ui_sm(6,  "f32");
            ci.members["corner_radius"] = ui_sm(7,  "f32");
            ci.members["padding_h"]     = ui_sm(8,  "f32");
            ci.members["padding_v"]     = ui_sm(9,  "f32");
            ci.members["opacity"]       = ui_sm(10, "f32");
            ci.members["font"]          = ui_sm(12, "ui_font");
            ci.members["padding_x"]     = ui_sm(11, "f32");
            ci.members["padding_y"]     = ui_sm(13, "f32");
            ci.members["size_w"]        = ui_sm(14, "f32");
            ci.members["size_h"]        = ui_sm(15, "f32");
            ctx.classes["ui_style"] = std::move(ci);
        }

        // ── ui namespace ─────────────────────────────────────────────────────
        auto& ui = ctx.namespaces["ui"];

        // Helper macro: style arg → store struct to alloca → pass as i8*
#define UI_STYLE_ARG(argIdx) \
        ([&]() -> llvm::Value* { \
            if ((int)args.size() <= (argIdx)) return null; \
            llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[argIdx])); \
            if (v->getType()->isPointerTy()) return v; \
            auto* tmp = ctx.createEntryAlloca(v->getType(), "ui_style_arg"); \
            ctx.builder.CreateStore(v, tmp); return tmp; \
        }())

        // Helper macro: buffer<f32> arg → store struct to alloca → pass as ptr
#define UI_BUF_ARG(argIdx) \
        ([&]() -> llvm::Value* { \
            if ((int)args.size() <= (argIdx)) return null; \
            llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[argIdx])); \
            if (v->getType()->isPointerTy()) return v; \
            auto* tmp = ctx.createEntryAlloca(v->getType(), "ui_buf_arg"); \
            ctx.builder.CreateStore(v, tmp); return tmp; \
        }())

        // Helper macro: get arg[i] as pointer (simple, for i8* args)
#define UI_PTR_ARG(i) \
        ((int)args.size() > (i) \
            ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])) : null)

        // Helper macro: get arg[i] as i32
#define UI_I32_ARG(i) \
        ((int)args.size() > (i) \
            ? ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])), i32) : ctx.builder.getInt32(0))

        // Helper macro: get arg[i] as f32
#define UI_F32_ARG(i) \
        ((int)args.size() > (i) \
            ? ctx.builder.CreateFPCast(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])), f32) : llvm::ConstantFP::get(f32, 0.0))

        // ui.init()
        ui.builtinFuncs["init"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_init",
                ctx.builder.getVoidTy(), {}), {});
            return nullptr; };

        // ui.begin(win: window_handle)
        ui.builtinFuncs["begin"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            llvm::Value* w = args.empty() ? null
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_begin",
                ctx.builder.getVoidTy(), {p}), {w});
            return nullptr; };

        // ui.end()
        ui.builtinFuncs["end"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_end",
                ctx.builder.getVoidTy(), {}), {});
            return nullptr; };

        // ui.begin_grid(cols: buffer<f32>, rows: buffer<f32>)
        ui.builtinFuncs["begin_grid"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            auto getArg = [&](int i) -> llvm::Value* {
                if ((int)args.size() <= i) return null;
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]));
                if (v->getType()->isPointerTy()) return v;
                auto* tmp = ctx.createEntryAlloca(v->getType(), "ui_gbuf");
                ctx.builder.CreateStore(v, tmp); return tmp;
            };
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_begin_grid",
                ctx.builder.getVoidTy(), {p, p}), {getArg(0), getArg(1)});
            return nullptr; };

        // ui.end_grid()
        ui.builtinFuncs["end_grid"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_end_grid",
                ctx.builder.getVoidTy(), {}), {});
            return nullptr; };

        // ui.set_gap(h: f32, v: f32)
        ui.builtinFuncs["set_gap"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* f32 = ctx.builder.getFloatTy();
            auto farg = [&](int i) { return (int)args.size() > i
                ? ctx.builder.CreateFPCast(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])), f32)
                : llvm::ConstantFP::get(f32, 0.0); };
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_set_gap",
                ctx.builder.getVoidTy(), {f32, f32}), {farg(0), farg(1)});
            return nullptr; };

        // ui.begin_cell(col, row, colspan, rowspan)
        ui.builtinFuncs["begin_cell"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* i32 = ctx.builder.getInt32Ty();
            auto iarg = [&](int i) { return (int)args.size() > i
                ? ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])), i32)
                : ctx.builder.getInt32(0); };
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_begin_cell",
                ctx.builder.getVoidTy(), {i32, i32, i32, i32}),
                {iarg(0), iarg(1), iarg(2), iarg(3)});
            return nullptr; };

        // ui.end_cell()
        ui.builtinFuncs["end_cell"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_end_cell",
                ctx.builder.getVoidTy(), {}), {});
            return nullptr; };

        // ui.set_align(h: string, v: string)
        ui.builtinFuncs["set_align"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            auto sarg = [&](int i) -> llvm::Value* {
                if ((int)args.size() <= i) return null;
                return static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]));
            };
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_set_align",
                ctx.builder.getVoidTy(), {p, p}), {sarg(0), sarg(1)});
            return nullptr; };

        // ui.set_scroll(axis: string) — "x", "y", or "xy"
        ui.builtinFuncs["set_scroll"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            llvm::Value* axis = args.empty() ? null
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_set_scroll",
                ctx.builder.getVoidTy(), {p}), {axis});
            return nullptr; };

        // ui.label(text: string, preset: ui_label_style = null)
        ui.builtinFuncs["label"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            llvm::Value* t = args.empty() ? null
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* preset = args.size() < 2 ? (llvm::Value*)null
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_label",
                ctx.builder.getVoidTy(), {p, p}), {t, preset});
            return nullptr; };

        // ui.label_colored(text: string, color: i32, preset: ui_label_style = null)
        ui.builtinFuncs["label_colored"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto* i32 = ctx.builder.getInt32Ty();
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            llvm::Value* t = args.empty() ? null
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* c = args.size() < 2 ? ctx.builder.getInt32(-1)
                : ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])), i32);
            llvm::Value* preset = args.size() < 3 ? (llvm::Value*)null
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]));
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_label_colored",
                ctx.builder.getVoidTy(), {p, i32, p}), {t, c, preset});
            return nullptr; };

        // ui.button(label: string, preset: ui_button_style = null) → bool (i32)
        ui.builtinFuncs["button"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            llvm::Value* t = args.empty() ? null
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* preset = args.size() < 2 ? (llvm::Value*)null
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_ui_button", ctx.builder.getInt32Ty(), {p, p}), {t, preset})); };

        // ui.button_icon(label: string, tex: gl_texture) → bool (i32)
        ui.builtinFuncs["button_icon"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            auto sarg = [&](int i) -> llvm::Value* {
                if ((int)args.size() <= i) return null;
                return static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]));
            };
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_ui_button_icon", ctx.builder.getInt32Ty(), {p, p}),
                {sarg(0), sarg(1)})); };

        // ui.checkbox(id, value: bool, preset: ui_button_style = null) → bool
        ui.builtinFuncs["checkbox"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto* i32 = ctx.builder.getInt32Ty();
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            llvm::Value* id  = args.empty() ? null : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* val = args.size() < 2 ? ctx.builder.getInt32(0)
                : ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])), i32);
            llvm::Value* preset = args.size() < 3 ? (llvm::Value*)null
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_ui_checkbox", i32, {p, i32, p}), {id, val, preset})); };

        // ui.spinbox(id, options: list<string>, current: i32, preset: ui_button_style = null) → i32
        ui.builtinFuncs["spinbox"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto* i32 = ctx.builder.getInt32Ty();
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            llvm::Value* id = args.empty() ? null : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            // Store the loaded list<string> struct into a temp alloca; pass pointer to C
            llvm::Type* listTy = ctx.getLLVMType("list<string>");
            llvm::AllocaInst* listAlloca = ctx.createEntryAlloca(listTy, "sbox_opts");
            if (args.size() >= 2) {
                ctx.builder.CreateStore(
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])), listAlloca);
            } else {
                ctx.builder.CreateStore(llvm::ConstantAggregateZero::get(listTy), listAlloca);
            }

            llvm::Value* cur = args.size() < 3 ? ctx.builder.getInt32(0)
                : ctx.builder.CreateZExtOrTrunc(
                    static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2])), i32);
            llvm::Value* preset = args.size() < 4 ? (llvm::Value*)null
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[3]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_ui_spinbox", i32, {p, p, i32, p}),
                {id, listAlloca, cur, preset})); };

        // ui.cell_hovered() → bool (i32)
        ui.builtinFuncs["cell_hovered"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_ui_cell_hovered", ctx.builder.getInt32Ty(), {}), {})); };

        // ui.toggle(id, value: bool, preset: ui_button_style = null) → bool
        ui.builtinFuncs["toggle"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto* i32 = ctx.builder.getInt32Ty();
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            llvm::Value* id  = args.empty() ? null : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* val = args.size() < 2 ? ctx.builder.getInt32(0)
                : ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])), i32);
            llvm::Value* preset = args.size() < 3 ? (llvm::Value*)null
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_ui_toggle", i32, {p, i32, p}), {id, val, preset})); };

        // ui.radio(id, option: i32, current: i32, preset: ui_button_style = null) → i32
        ui.builtinFuncs["radio"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto* i32 = ctx.builder.getInt32Ty();
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            auto iarg = [&](int i) -> llvm::Value* {
                if ((int)args.size() <= i) return ctx.builder.getInt32(0);
                return ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])), i32);
            };
            llvm::Value* id = args.empty() ? null : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* preset = args.size() < 4 ? (llvm::Value*)null
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[3]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_ui_radio", i32, {p, i32, i32, p}),
                {id, iarg(1), iarg(2), preset})); };

        // ui.slider(id, value: f32, min: f32, max: f32, preset: ui_slider_style = null) → f32
        ui.builtinFuncs["slider"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto* f32 = ctx.builder.getFloatTy();
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            auto farg = [&](int i) -> llvm::Value* {
                if ((int)args.size() <= i) return llvm::ConstantFP::get(f32, 0.0);
                return ctx.builder.CreateFPCast(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])), f32);
            };
            llvm::Value* id = args.empty() ? null : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* preset = args.size() < 5 ? (llvm::Value*)null
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[4]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_ui_slider", f32, {p, f32, f32, f32, p}),
                {id, farg(1), farg(2), farg(3), preset})); };

        // ui.slider_int(id, value: i32, min: i32, max: i32, preset: ui_slider_style = null) → i32
        ui.builtinFuncs["slider_int"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto* i32 = ctx.builder.getInt32Ty();
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            auto iarg = [&](int i) -> llvm::Value* {
                if ((int)args.size() <= i) return ctx.builder.getInt32(0);
                return ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])), i32);
            };
            llvm::Value* id = args.empty() ? null : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* preset = args.size() < 5 ? (llvm::Value*)null
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[4]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_ui_slider_int", i32, {p, i32, i32, i32, p}),
                {id, iarg(1), iarg(2), iarg(3), preset})); };

        // ui.drag_f32(id, value: f32, speed: f32, preset: ui_slider_style = null) → f32
        ui.builtinFuncs["drag_f32"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto* f32 = ctx.builder.getFloatTy();
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            auto farg = [&](int i) -> llvm::Value* {
                if ((int)args.size() <= i) return llvm::ConstantFP::get(f32, 0.0);
                return ctx.builder.CreateFPCast(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])), f32);
            };
            llvm::Value* id = args.empty() ? null : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* preset = args.size() < 4 ? (llvm::Value*)null
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[3]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_ui_drag_f32", f32, {p, f32, f32, p}),
                {id, farg(1), farg(2), preset})); };

        // ui.password_input(id, buf, preset = null) → string — text_input drawn as bullets (spec/44_3 F44-3-20)
        ui.builtinFuncs["password_input"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            auto sarg = [&](int i) -> llvm::Value* {
                if ((int)args.size() <= i) return null;
                return static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]));
            };
            llvm::Value* preset = args.size() < 3 ? (llvm::Value*)null : sarg(2);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_ui_password_input", p, {p, p, p}), {sarg(0), sarg(1), preset})); };

        // ui.password_mask(text) → string — the caption password_input draws (F44-3-21)
        ui.builtinFuncs["password_mask"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* text = args.empty()
                ? (llvm::Value*)llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p))
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_ui_password_mask", p, {p}), {text})); };

        // ui.text_input(id: string, buf: string, preset: ui_label_style = null) → string
        ui.builtinFuncs["text_input"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            auto sarg = [&](int i) -> llvm::Value* {
                if ((int)args.size() <= i) return null;
                return static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]));
            };
            llvm::Value* preset = args.size() < 3 ? (llvm::Value*)null : sarg(2);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_ui_text_input", p, {p, p, p}), {sarg(0), sarg(1), preset})); };

        // ui.text_area(id: string, buf: string, preset: ui_label_style = null) → string
        ui.builtinFuncs["text_area"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            auto sarg = [&](int i) -> llvm::Value* {
                if ((int)args.size() <= i) return null;
                return static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]));
            };
            llvm::Value* preset = args.size() < 3 ? (llvm::Value*)null : sarg(2);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_ui_text_area", p, {p, p, p}), {sarg(0), sarg(1), preset})); };

        // ui.progress_bar(value: f32, preset: ui_progress_style = null)
        ui.builtinFuncs["progress_bar"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* f32 = ctx.builder.getFloatTy();
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            llvm::Value* v = args.empty() ? llvm::ConstantFP::get(f32, 0.0)
                : ctx.builder.CreateFPCast(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])), f32);
            llvm::Value* preset = args.size() < 2 ? (llvm::Value*)null
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_progress_bar",
                ctx.builder.getVoidTy(), {f32, p}), {v, preset});
            return nullptr; };

        // ui.progress_bar_colored(value: f32, fg: i32, bg: i32, preset: ui_progress_style = null)
        ui.builtinFuncs["progress_bar_colored"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* f32 = ctx.builder.getFloatTy();
            auto* i32 = ctx.builder.getInt32Ty();
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            auto farg = [&](int i) -> llvm::Value* {
                if ((int)args.size() <= i) return llvm::ConstantFP::get(f32, 0.0);
                return ctx.builder.CreateFPCast(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])), f32);
            };
            auto iarg = [&](int i) -> llvm::Value* {
                if ((int)args.size() <= i) return ctx.builder.getInt32(0);
                return ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])), i32);
            };
            llvm::Value* preset = args.size() < 4 ? (llvm::Value*)null
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[3]));
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_progress_bar_colored",
                ctx.builder.getVoidTy(), {f32, i32, i32, p}), {farg(0), iarg(1), iarg(2), preset});
            return nullptr; };

        // ui.image(tex: gl_texture)
        ui.builtinFuncs["image"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            llvm::Value* t = args.empty() ? null : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_image",
                ctx.builder.getVoidTy(), {p}), {t});
            return nullptr; };

        // ui.image_sized(tex: gl_texture, w: f32, h: f32)
        ui.builtinFuncs["image_sized"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto* f32 = ctx.builder.getFloatTy();
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            auto farg = [&](int i) -> llvm::Value* {
                if ((int)args.size() <= i) return llvm::ConstantFP::get(f32, 0.0);
                return ctx.builder.CreateFPCast(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])), f32);
            };
            llvm::Value* t = args.empty() ? null : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_image_sized",
                ctx.builder.getVoidTy(), {p, f32, f32}), {t, farg(1), farg(2)});
            return nullptr; };

        // ui.rect(color: i32)
        ui.builtinFuncs["rect"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Value* c = args.empty() ? ctx.builder.getInt32(0)
                : ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])), i32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_rect",
                ctx.builder.getVoidTy(), {i32}), {c});
            return nullptr; };

        // ui.rect_rounded(color: i32, radius: f32, preset: ui_panel_style = null)
        ui.builtinFuncs["rect_rounded"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* i32 = ctx.builder.getInt32Ty();
            auto* f32 = ctx.builder.getFloatTy();
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            llvm::Value* c = args.empty() ? ctx.builder.getInt32(0)
                : ctx.builder.CreateZExtOrTrunc(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])), i32);
            llvm::Value* r = args.size() < 2 ? llvm::ConstantFP::get(f32, 0.0)
                : ctx.builder.CreateFPCast(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])), f32);
            llvm::Value* preset = args.size() < 3 ? (llvm::Value*)null
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]));
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_rect_rounded",
                ctx.builder.getVoidTy(), {i32, f32, p}), {c, r, preset});
            return nullptr; };

        // ui.separator(), ui.same_line(), ui.newline()
        ui.builtinFuncs["separator"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_separator",
                ctx.builder.getVoidTy(), {}), {}); return nullptr; };
        ui.builtinFuncs["same_line"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_same_line",
                ctx.builder.getVoidTy(), {}), {}); return nullptr; };
        ui.builtinFuncs["newline"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_newline",
                ctx.builder.getVoidTy(), {}), {}); return nullptr; };

        // ui.spacer(size: f32)
        ui.builtinFuncs["spacer"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* f32 = ctx.builder.getFloatTy();
            llvm::Value* s = args.empty() ? llvm::ConstantFP::get(f32, 0.0)
                : ctx.builder.CreateFPCast(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])), f32);
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_spacer",
                ctx.builder.getVoidTy(), {f32}), {s});
            return nullptr; };

        // ui.begin_notebook(id: string) → i32 (active tab index)
        ui.builtinFuncs["begin_notebook"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            llvm::Value* id = args.empty() ? null
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_ui_begin_notebook", ctx.builder.getInt32Ty(), {p}), {id})); };

        // ui.notebook_tab(label: string) → i32 (1 if active)
        ui.builtinFuncs["notebook_tab"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            llvm::Value* lbl = args.empty() ? null
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_ui_notebook_tab", ctx.builder.getInt32Ty(), {p}), {lbl})); };

        // ui.end_notebook()
        ui.builtinFuncs["end_notebook"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_end_notebook",
                ctx.builder.getVoidTy(), {}), {}); return nullptr; };

        // ui.notebook_remove_clicked(id: string) → i32
        ui.builtinFuncs["notebook_remove_clicked"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            llvm::Value* id = args.empty() ? null
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_ui_notebook_remove_clicked", ctx.builder.getInt32Ty(), {p}), {id})); };

        // ui.push_style(s: ui_style) — style is a struct value, pass by alloca ptr
        ui.builtinFuncs["push_style"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            llvm::Value* sp = null;
            if (!args.empty()) {
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                if (v->getType()->isPointerTy()) sp = v;
                else { auto* tmp = ctx.createEntryAlloca(v->getType(), "ui_ps");
                       ctx.builder.CreateStore(v, tmp); sp = tmp; }
            }
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_push_style",
                ctx.builder.getVoidTy(), {p}), {sp});
            return nullptr; };

        // ui.pop_style()
        ui.builtinFuncs["pop_style"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_pop_style",
                ctx.builder.getVoidTy(), {}), {}); return nullptr; };

        // ui.set_default_style(s: ui_style)
        ui.builtinFuncs["set_default_style"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            llvm::Value* sp = null;
            if (!args.empty()) {
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                if (v->getType()->isPointerTy()) sp = v;
                else { auto* tmp = ctx.createEntryAlloca(v->getType(), "ui_ds");
                       ctx.builder.CreateStore(v, tmp); sp = tmp; }
            }
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_set_default_style",
                ctx.builder.getVoidTy(), {p}), {sp});
            return nullptr; };

        // ui.load_font(path: path, size: f32) → ui_font
        ui.builtinFuncs["load_font"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto* f32 = ctx.builder.getFloatTy();
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            llvm::Value* path = args.empty() ? null : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* sz   = args.size() < 2 ? llvm::ConstantFP::get(f32, 16.0)
                : ctx.builder.CreateFPCast(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])), f32);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_ui_load_font", p, {p, f32}), {path, sz})); };

        // ui.load_font_default(size: f32) → ui_font
        ui.builtinFuncs["load_font_default"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto* f32 = ctx.builder.getFloatTy();
            llvm::Value* sz = args.empty() ? llvm::ConstantFP::get(f32, 16.0)
                : ctx.builder.CreateFPCast(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])), f32);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                ctx.getFunction("aiml_ui_load_font_default", p, {f32}), {sz})); };

        // ui.set_default_font(font: ui_font)
        ui.builtinFuncs["set_default_font"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            auto  null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p));
            llvm::Value* f = args.empty() ? null : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            ctx.builder.CreateCall(ctx.getFunction("aiml_ui_set_default_font",
                ctx.builder.getVoidTy(), {p}), {f});
            return nullptr; };

        // ui.slider_style_new() → ui_slider_style
        // ui.button_style_new() → ui_button_style
        // ui.label_style_new()  → ui_label_style
        // ui.progress_style_new() → ui_progress_style
        // ui.panel_style_new()  → ui_panel_style
        {
            auto mk_factory = [](const std::string& fn) -> CompilerContext::FuncHandler {
                return [fn](CompilerContext& ctx, const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
                    auto* pp = llvm::PointerType::get(ctx.context, 0);
                    return ctx.ir_gen->wrap(ctx.builder.CreateCall(ctx.getFunction(fn, pp, {}), {}));
                };
            };
            ui.builtinFuncs["slider_style_new"]   = mk_factory("aiml_ui_slider_style_new");
            ui.builtinFuncs["button_style_new"]   = mk_factory("aiml_ui_button_style_new");
            ui.builtinFuncs["label_style_new"]    = mk_factory("aiml_ui_label_style_new");
            ui.builtinFuncs["progress_style_new"] = mk_factory("aiml_ui_progress_style_new");
            ui.builtinFuncs["panel_style_new"]    = mk_factory("aiml_ui_panel_style_new");
        }

        (void)p; (void)i8p; (void)i32; (void)f32; (void)vd; (void)null;
#undef UI_STYLE_ARG
#undef UI_BUF_ARG
#undef UI_PTR_ARG
#undef UI_I32_ARG
#undef UI_F32_ARG
    } // end ui

    // ── Widget Style Preset Classes ───────────────────────────────────────────
    {
        using MH = CompilerContext::MethodHandler;
        using CI = CompilerContext::ClassInfo;
        using EvalFn = std::function<AIML::IRValue*(AIML::AST::Expr*)>;

        auto mk_i32_set = [](const std::string& fn) -> MH {
            return [fn](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr* call, EvalFn evalExpr) -> AIML::IRValue* {
                auto* pp  = llvm::PointerType::get(ctx.context, 0);
                auto* i32 = ctx.builder.getInt32Ty();
                auto* vT  = llvm::Type::getVoidTy(ctx.context);
                llvm::Value* arg = call->args.empty()
                    ? ctx.builder.getInt32(0)
                    : ctx.builder.CreateZExtOrTrunc(
                          static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get()))), i32);
                ctx.builder.CreateCall(ctx.getFunction(fn, vT, {pp, i32}), {base, arg});
                return nullptr;
            };
        };
        auto mk_f32_set = [](const std::string& fn) -> MH {
            return [fn](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr* call, EvalFn evalExpr) -> AIML::IRValue* {
                auto* pp  = llvm::PointerType::get(ctx.context, 0);
                auto* f32 = ctx.builder.getFloatTy();
                auto* vT  = llvm::Type::getVoidTy(ctx.context);
                llvm::Value* raw = call->args.empty()
                    ? llvm::ConstantFP::get(f32, 0.0)
                    : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
                llvm::Value* arg = raw->getType()->isFloatTy() ? raw : ctx.builder.CreateFPCast(raw, f32);
                ctx.builder.CreateCall(ctx.getFunction(fn, vT, {pp, f32}), {base, arg});
                return nullptr;
            };
        };
        auto mk_ptr_set = [](const std::string& fn) -> MH {
            return [fn](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr* call, EvalFn evalExpr) -> AIML::IRValue* {
                auto* pp   = llvm::PointerType::get(ctx.context, 0);
                auto* vT   = llvm::Type::getVoidTy(ctx.context);
                auto* null = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(pp));
                llvm::Value* arg = call->args.empty()
                    ? static_cast<llvm::Value*>(null)
                    : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
                ctx.builder.CreateCall(ctx.getFunction(fn, vT, {pp, pp}), {base, arg});
                return nullptr;
            };
        };
        auto add_common = [&](CI& ci, const std::string& pfx) {
            ci.builtinMethods["set_color"]         = mk_i32_set(pfx + "_set_color");
            ci.builtinMethods["set_bg_color"]      = mk_i32_set(pfx + "_set_bg_color");
            ci.builtinMethods["set_hover_color"]   = mk_i32_set(pfx + "_set_hover_color");
            ci.builtinMethods["set_active_color"]  = mk_i32_set(pfx + "_set_active_color");
            ci.builtinMethods["set_border_color"]  = mk_i32_set(pfx + "_set_border_color");
            ci.builtinMethods["set_corner_radius"] = mk_f32_set(pfx + "_set_corner_radius");
            ci.builtinMethods["set_border_width"]  = mk_f32_set(pfx + "_set_border_width");
            ci.builtinMethods["set_font"]          = mk_ptr_set(pfx + "_set_font");
            ci.builtinMethods["set_padding_x"]     = mk_f32_set(pfx + "_set_padding_x");
            ci.builtinMethods["set_padding_y"]     = mk_f32_set(pfx + "_set_padding_y");
            ci.builtinMethods["set_size_w"]        = mk_f32_set(pfx + "_set_size_w");
            ci.builtinMethods["set_size_h"]        = mk_f32_set(pfx + "_set_size_h");
        };

        // ui_slider_style
        {
            CI ci; ci.name = "ui_slider_style"; ci.isBuiltin = true;
            ci.preLoadBase = true;
            add_common(ci, "aiml_ui_slider_style");
            ci.builtinMethods["set_thumb"] = mk_ptr_set("aiml_ui_slider_style_set_thumb");
            ci.builtinMethods["set_track"] = mk_ptr_set("aiml_ui_slider_style_set_track");
            ctx.classes["ui_slider_style"] = ci;
        }
        // ui_button_style
        {
            CI ci; ci.name = "ui_button_style"; ci.isBuiltin = true;
            ci.preLoadBase = true;
            add_common(ci, "aiml_ui_button_style");
            ci.builtinMethods["set_bg"]     = mk_ptr_set("aiml_ui_button_style_set_bg");
            ci.builtinMethods["set_hover"]  = mk_ptr_set("aiml_ui_button_style_set_hover");
            ci.builtinMethods["set_active"] = mk_ptr_set("aiml_ui_button_style_set_active");
            ctx.classes["ui_button_style"] = ci;
        }
        // ui_label_style
        {
            CI ci; ci.name = "ui_label_style"; ci.isBuiltin = true;
            ci.preLoadBase = true;
            add_common(ci, "aiml_ui_label_style");
            ctx.classes["ui_label_style"] = ci;
        }
        // ui_progress_style
        {
            CI ci; ci.name = "ui_progress_style"; ci.isBuiltin = true;
            ci.preLoadBase = true;
            add_common(ci, "aiml_ui_progress_style");
            ci.builtinMethods["set_fill"]   = mk_ptr_set("aiml_ui_progress_style_set_fill");
            ci.builtinMethods["set_bg_tex"] = mk_ptr_set("aiml_ui_progress_style_set_bg_tex");
            ctx.classes["ui_progress_style"] = ci;
        }
        // ui_panel_style
        {
            CI ci; ci.name = "ui_panel_style"; ci.isBuiltin = true;
            ci.preLoadBase = true;
            add_common(ci, "aiml_ui_panel_style");
            ci.builtinMethods["set_bg_tex"] = mk_ptr_set("aiml_ui_panel_style_set_bg_tex");
            ctx.classes["ui_panel_style"] = ci;
        }
    }

    // ── audio_sound + audio_group builtin classes + audio namespace ───────────
    {
        using CI = CompilerContext::ClassInfo;
        using MH = CompilerContext::MethodHandler;

        llvm::Type* i8p  = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32  = ctx.builder.getInt32Ty();
        llvm::Type* f32  = ctx.builder.getFloatTy();
        llvm::Type* vd   = ctx.builder.getVoidTy();
        llvm::StructType* vec3Ty = static_cast<llvm::StructType*>(ctx.getLLVMType("vec3"));

        // Helper: store vec3 struct value into a temp alloca, return the alloca ptr
        auto storeVec3 = [vec3Ty](CompilerContext& ctx2, llvm::Value* v) -> llvm::Value* {
            llvm::AllocaInst* tmp = ctx2.builder.CreateAlloca(vec3Ty);
            ctx2.builder.CreateStore(v, tmp);
            return tmp;
        };

        // ── audio_sound class ────────────────────────────────────────────────
        {
            CI ci; ci.name = "audio_sound"; ci.isBuiltin = true;

            auto mk0v = [](const char* fn_name) -> MH {
                return [fn_name](CompilerContext& ctx, llvm::Value* base,
                    AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
                    llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                    llvm::Value* p  = ctx.builder.CreateLoad(i8p, base);
                    llvm::Function* f = ctx.getFunction(fn_name, ctx.builder.getVoidTy(), {i8p});
                    ctx.builder.CreateCall(f, {p});
                    return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
                };
            };
            auto mk0i = [](const char* fn_name) -> MH {
                return [fn_name](CompilerContext& ctx, llvm::Value* base,
                    AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
                    llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                    llvm::Value* p  = ctx.builder.CreateLoad(i8p, base);
                    llvm::Function* f = ctx.getFunction(fn_name, ctx.builder.getInt32Ty(), {i8p});
                    return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {p}));
                };
            };
            auto mk0f = [](const char* fn_name) -> MH {
                return [fn_name](CompilerContext& ctx, llvm::Value* base,
                    AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
                    llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                    llvm::Value* p  = ctx.builder.CreateLoad(i8p, base);
                    llvm::Function* f = ctx.getFunction(fn_name, ctx.builder.getFloatTy(), {i8p});
                    return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {p}));
                };
            };
            auto mk1i = [](const char* fn_name) -> MH {
                return [fn_name](CompilerContext& ctx, llvm::Value* base,
                    AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
                    llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                    llvm::Type* i32 = ctx.builder.getInt32Ty();
                    llvm::Value* p  = ctx.builder.CreateLoad(i8p, base);
                    llvm::Value* a0 = call->args.empty() ? ctx.builder.getInt32(0)
                        : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
                    llvm::Function* f = ctx.getFunction(fn_name, ctx.builder.getVoidTy(), {i8p, i32});
                    ctx.builder.CreateCall(f, {p, a0});
                    return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
                };
            };
            auto mk1f = [](const char* fn_name) -> MH {
                return [fn_name](CompilerContext& ctx, llvm::Value* base,
                    AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
                    llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                    llvm::Type* f32 = ctx.builder.getFloatTy();
                    llvm::Value* p  = ctx.builder.CreateLoad(i8p, base);
                    llvm::Value* a0 = call->args.empty()
                        ? llvm::ConstantFP::get(f32, 0.0)
                        : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
                    // ensure f32 even if caller passed f64
                    if (a0->getType()->isDoubleTy())
                        a0 = ctx.builder.CreateFPTrunc(a0, f32);
                    llvm::Function* f = ctx.getFunction(fn_name, ctx.builder.getVoidTy(), {i8p, f32});
                    ctx.builder.CreateCall(f, {p, a0});
                    return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
                };
            };

            ci.builtinMethods["play"]    = mk0v("aiml_audio_sound_play");
            ci.builtinMethods["pause"]   = mk0v("aiml_audio_sound_pause");
            ci.builtinMethods["stop"]    = mk0v("aiml_audio_sound_stop");
            ci.builtinMethods["free"]    = mk0v("aiml_audio_sound_free");
            ci.builtinMethods["is_playing"]  = mk0i("aiml_audio_sound_is_playing");
            ci.builtinMethods["is_finished"] = mk0i("aiml_audio_sound_is_finished");
            ci.builtinMethods["duration"]    = mk0f("aiml_audio_sound_duration");
            ci.builtinMethods["position"]    = mk0f("aiml_audio_sound_position");
            ci.builtinMethods["set_loop"]    = mk1i("aiml_audio_sound_set_loop");
            ci.builtinMethods["set_volume"]  = mk1f("aiml_audio_sound_set_volume");
            ci.builtinMethods["set_pitch"]   = mk1f("aiml_audio_sound_set_pitch");
            ci.builtinMethods["set_pan"]     = mk1f("aiml_audio_sound_set_pan");
            ci.builtinMethods["seek"]        = mk1f("aiml_audio_sound_seek");

            // set_group(group: audio_group)
            ci.builtinMethods["set_group"] = [](CompilerContext& ctx, llvm::Value* base,
                AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Value* p  = ctx.builder.CreateLoad(i8p, base);
                if (call->args.empty()) return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
                llvm::Value* g = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
                if (g->getType()->isPointerTy() && !g->getType()->getPointerAddressSpace()
                    && g->getType() == i8p) {
                    // already i8*
                } else if (!g->getType()->isPointerTy()) {
                    // loaded value — store to alloca and load as pointer
                    llvm::AllocaInst* tmp = ctx.createEntryAlloca(g->getType());
                    ctx.builder.CreateStore(g, tmp);
                    g = ctx.builder.CreateLoad(i8p, tmp);
                }
                llvm::Function* f = ctx.getFunction("aiml_audio_sound_set_group",
                                                    ctx.builder.getVoidTy(), {i8p, i8p});
                ctx.builder.CreateCall(f, {p, g});
                return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
            };

            // set_position(pos: vec3)
            ci.builtinMethods["set_position"] = [storeVec3, vec3Ty](
                CompilerContext& ctx, llvm::Value* base,
                AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Type* f32 = ctx.builder.getFloatTy();
                llvm::Value* p  = ctx.builder.CreateLoad(i8p, base);
                if (call->args.empty()) return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
                if (v->getType() != vec3Ty) return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
                llvm::Value* x = ctx.builder.CreateExtractValue(v, {0u});
                llvm::Value* y = ctx.builder.CreateExtractValue(v, {1u});
                llvm::Value* z = ctx.builder.CreateExtractValue(v, {2u});
                llvm::Function* f = ctx.getFunction("aiml_audio_sound_set_position",
                    ctx.builder.getVoidTy(), {i8p, f32, f32, f32});
                ctx.builder.CreateCall(f, {p, x, y, z});
                return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
            };

            // set_velocity(vel: vec3)
            ci.builtinMethods["set_velocity"] = [storeVec3, vec3Ty](
                CompilerContext& ctx, llvm::Value* base,
                AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Type* f32 = ctx.builder.getFloatTy();
                llvm::Value* p  = ctx.builder.CreateLoad(i8p, base);
                if (call->args.empty()) return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
                if (v->getType() != vec3Ty) return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
                llvm::Value* x = ctx.builder.CreateExtractValue(v, {0u});
                llvm::Value* y = ctx.builder.CreateExtractValue(v, {1u});
                llvm::Value* z = ctx.builder.CreateExtractValue(v, {2u});
                llvm::Function* f = ctx.getFunction("aiml_audio_sound_set_velocity",
                    ctx.builder.getVoidTy(), {i8p, f32, f32, f32});
                ctx.builder.CreateCall(f, {p, x, y, z});
                return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
            };

            // set_attenuation(model: string, min_dist: f32, max_dist: f32)
            ci.builtinMethods["set_attenuation"] = [](
                CompilerContext& ctx, llvm::Value* base,
                AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Type* f32 = ctx.builder.getFloatTy();
                llvm::Value* p  = ctx.builder.CreateLoad(i8p, base);
                if (call->args.size() < 3) return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
                llvm::Value* model    = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
                llvm::Value* min_dist = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
                llvm::Value* max_dist = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[2].get())));
                if (min_dist->getType()->isDoubleTy()) min_dist = ctx.builder.CreateFPTrunc(min_dist, f32);
                if (max_dist->getType()->isDoubleTy()) max_dist = ctx.builder.CreateFPTrunc(max_dist, f32);
                llvm::Function* f = ctx.getFunction("aiml_audio_sound_set_attenuation",
                    ctx.builder.getVoidTy(), {i8p, i8p, f32, f32});
                ctx.builder.CreateCall(f, {p, model, min_dist, max_dist});
                return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
            };

            // set_spatialization(on: bool/i32)
            ci.builtinMethods["set_spatialization"] = mk1i("aiml_audio_sound_set_spatialization");

            // set_directional_attenuation(factor: f32)
            ci.builtinMethods["set_directional_attenuation"] = mk1f("aiml_audio_sound_set_directional_attenuation");

            ctx.classes["audio_sound"] = std::move(ci);
        }

        // ── audio_group class ────────────────────────────────────────────────
        {
            CI ci; ci.name = "audio_group"; ci.isBuiltin = true;

            auto mk0v = [](const char* fn_name) -> MH {
                return [fn_name](CompilerContext& ctx, llvm::Value* base,
                    AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
                    llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                    llvm::Value* p  = ctx.builder.CreateLoad(i8p, base);
                    llvm::Function* f = ctx.getFunction(fn_name, ctx.builder.getVoidTy(), {i8p});
                    ctx.builder.CreateCall(f, {p});
                    return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
                };
            };
            auto mk1f = [](const char* fn_name) -> MH {
                return [fn_name](CompilerContext& ctx, llvm::Value* base,
                    AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
                    llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                    llvm::Type* f32 = ctx.builder.getFloatTy();
                    llvm::Value* p  = ctx.builder.CreateLoad(i8p, base);
                    llvm::Value* a0 = call->args.empty()
                        ? llvm::ConstantFP::get(f32, 1.0)
                        : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
                    if (a0->getType()->isDoubleTy()) a0 = ctx.builder.CreateFPTrunc(a0, f32);
                    llvm::Function* f = ctx.getFunction(fn_name, ctx.builder.getVoidTy(), {i8p, f32});
                    ctx.builder.CreateCall(f, {p, a0});
                    return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
                };
            };

            ci.builtinMethods["set_volume"]  = mk1f("aiml_audio_group_set_volume");
            ci.builtinMethods["pause_all"]   = mk0v("aiml_audio_group_pause_all");
            ci.builtinMethods["resume_all"]  = mk0v("aiml_audio_group_resume_all");
            ci.builtinMethods["stop_all"]    = mk0v("aiml_audio_group_stop_all");

            ctx.classes["audio_group"] = std::move(ci);
        }

        // ── audio namespace ──────────────────────────────────────────────────
        {
            auto& ns = ctx.namespaces["audio"];

            auto toF32 = [](CompilerContext& ctx, llvm::Value* v) -> llvm::Value* {
                if (v->getType()->isDoubleTy()) return ctx.builder.CreateFPTrunc(v, ctx.builder.getFloatTy());
                return v;
            };

            // audio.sfx(stem) / audio.music(stem) → audio_handle (spec/44_7).
            auto mkAssetAudio = [](const char* runtime_fn, AIML::AssetKind kind,
                                   const char* method) {
                return [runtime_fn, kind, method](CompilerContext& ctx,
                    const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                    auto* p = llvm::PointerType::get(ctx.context, 0);
                    if (!args.empty() && ctx.currentCall &&
                        !ctx.currentCall->args.empty()) {
                        if (auto* lit = dynamic_cast<AIML::AST::LiteralExpr*>(
                                ctx.currentCall->args[0].get())) {
                            if (lit->type == "string" &&
                                !AIML::AssetScanner::instance().has_stem(kind, lit->value)) {
                                LOG_ERROR(ctx.fmtLoc() + std::string("audio.") + method +
                                          ": stem '" + lit->value +
                                          "' not in audio store");
                            }
                        }
                    }
                    llvm::Value* arg = args.empty()
                        ? llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(p))
                        : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                    return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                        ctx.getFunction(runtime_fn, p, {p}), {arg}));
                };
            };
            ns.builtinFuncs["sfx"]   = mkAssetAudio("aiml_asset_get_sfx",   AIML::AssetKind::Audio, "sfx");
            ns.builtinFuncs["music"] = mkAssetAudio("aiml_asset_get_music", AIML::AssetKind::Audio, "music");

            // audio.init()
            ns.builtinFuncs["init"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
                llvm::Function* f = ctx.getFunction("aiml_audio_init", ctx.builder.getVoidTy(), {});
                ctx.builder.CreateCall(f, {});
                return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
            };

            // audio.shutdown()
            ns.builtinFuncs["shutdown"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
                llvm::Function* f = ctx.getFunction("aiml_audio_shutdown", ctx.builder.getVoidTy(), {});
                ctx.builder.CreateCall(f, {});
                return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
            };

            // audio.set_volume(v: f32)
            ns.builtinFuncs["set_volume"] = [toF32](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                llvm::Type* f32 = ctx.builder.getFloatTy();
                llvm::Value* v = args.empty()
                    ? llvm::ConstantFP::get(f32, 1.0)
                    : toF32(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])));
                llvm::Function* f = ctx.getFunction("aiml_audio_set_volume", ctx.builder.getVoidTy(), {f32});
                ctx.builder.CreateCall(f, {v});
                return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
            };

            // audio.load(p: path) → audio_sound (i8*)
            ns.builtinFuncs["load"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Value* path = args.empty()
                    ? llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p))
                    : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                llvm::Function* f = ctx.getFunction("aiml_audio_load", i8p, {i8p});
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {path}));
            };

            // audio.load_stream(p: path) → audio_sound (i8*)
            ns.builtinFuncs["load_stream"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Value* path = args.empty()
                    ? llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p))
                    : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                llvm::Function* f = ctx.getFunction("aiml_audio_load_stream", i8p, {i8p});
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {path}));
            };

            // audio.create_group() → audio_group (i8*)
            ns.builtinFuncs["create_group"] = [](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* f = ctx.getFunction("aiml_audio_create_group", i8p, {});
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {}));
            };

            // audio.set_listener(pos: vec3, forward: vec3, up: vec3)
            ns.builtinFuncs["set_listener"] = [vec3Ty](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                llvm::Type* f32 = ctx.builder.getFloatTy();
                if (args.size() < 3) return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
                auto extract = [&](AIML::IRValue* irv, unsigned idx) -> llvm::Value* {
                    llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(irv));
                    return ctx.builder.CreateExtractValue(v, {idx});
                };
                llvm::Value* px = extract(args[0], 0u); llvm::Value* py = extract(args[0], 1u); llvm::Value* pz = extract(args[0], 2u);
                llvm::Value* fx = extract(args[1], 0u); llvm::Value* fy = extract(args[1], 1u); llvm::Value* fz = extract(args[1], 2u);
                llvm::Value* ux = extract(args[2], 0u); llvm::Value* uy = extract(args[2], 1u); llvm::Value* uz = extract(args[2], 2u);
                llvm::Function* fn = ctx.getFunction("aiml_audio_set_listener",
                    ctx.builder.getVoidTy(), {f32,f32,f32, f32,f32,f32, f32,f32,f32});
                ctx.builder.CreateCall(fn, {px,py,pz, fx,fy,fz, ux,uy,uz});
                return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
            };

            // audio.set_listener_velocity(vel: vec3)
            ns.builtinFuncs["set_listener_velocity"] = [vec3Ty](CompilerContext& ctx,
                const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                llvm::Type* f32 = ctx.builder.getFloatTy();
                if (args.empty()) return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                llvm::Value* vx = ctx.builder.CreateExtractValue(v, {0u});
                llvm::Value* vy = ctx.builder.CreateExtractValue(v, {1u});
                llvm::Value* vz = ctx.builder.CreateExtractValue(v, {2u});
                llvm::Function* fn = ctx.getFunction("aiml_audio_set_listener_velocity",
                    ctx.builder.getVoidTy(), {f32, f32, f32});
                ctx.builder.CreateCall(fn, {vx, vy, vz});
                return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
            };
        }
    }
    // ── end audio ──────────────────────────────────────────────────────────────

    // ── mirror_event + mirror_peer + mirror_session + mirror namespace ─────────
    // mirror_event: real LLVM struct {i8*(type,0), i8*(peer,1), i32(net_id,2)}
    // mirror_peer:  opaque i8* handle (no methods in v1)
    // mirror_session: opaque i8* handle, preLoadBase=false
    // ──────────────────────────────────────────────────────────────────────────
    {
        using CI     = CompilerContext::ClassInfo;
        using MH     = CompilerContext::MethodHandler;
        using EvalFn = std::function<AIML::IRValue*(AIML::AST::Expr*)>;

        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Type* i32 = ctx.builder.getInt32Ty();
        llvm::Type* i64 = ctx.builder.getInt64Ty();
        llvm::Type* sz  = ctx.getSizeType();
        llvm::Type* f32 = ctx.builder.getFloatTy();
        llvm::Type* vd  = ctx.builder.getVoidTy();

        // ── mirror_event (isBuiltin=false — real struct for field GEP access) ──
        // Layout MUST match AimlMirrorEvent in mirror_runtime.cpp:
        //   { i8* type, i8* peer, i32 net_id, i32 peer_id }
        // peer_id is the unique per-connection sender id assigned at accept-
        // time by the host (1..N); use this (not net_id) to identify which
        // client a peer_connected / peer_disconnected event refers to.
        {
            llvm::StructType* evTy = llvm::StructType::getTypeByName(ctx.context, "mirror_event");
            if (!evTy)
                evTy = llvm::StructType::create(ctx.context, {i8p, i8p, i32, i32}, "mirror_event");

            CI ci; ci.name = "mirror_event"; ci.type = evTy; ci.isBuiltin = false;
            auto mk = [](llvm::Type* t, int idx, const std::string& tn) {
                CompilerContext::StructMember m; m.type = t; m.index = idx; m.typeName = tn; return m;
            };
            ci.members["type"]    = mk(i8p, 0, "string");
            ci.members["peer"]    = mk(i8p, 1, "mirror_peer");
            ci.members["net_id"]  = mk(i32, 2, "i32");
            ci.members["peer_id"] = mk(i32, 3, "i32");
            // NB: type_enum() is dispatched as a special-case in
            // ast_compiler_call.cpp (mirror_event is isBuiltin=false, so its
            // builtinMethods map is not consulted).
            ctx.classes["mirror_event"] = std::move(ci);
        }

        // ── rpc_event (isBuiltin=false — real struct, layout MUST mirror AimlRpcEvent in mirror_runtime.cpp)
        //   { i8* method, i32 net_id, i32 arg, i32 arg_kind, i8* arg_blob, i32 arg_blob_len, i32 peer_id,
        //     i32 kind, i64 request_id } — one definition: CompilerContext::rpcEventType().
        //
        // peer_id is the unique per-connection sender id (host-assigned at
        // accept-time); multi-client servers MUST use this — NOT net_id —
        // to identify the sender, since clients can race-allocate the same
        // net_id for their respective AuthRequest objects.
        {
            llvm::StructType* evTy = ctx.rpcEventType();

            CI ci; ci.name = "rpc_event"; ci.type = evTy; ci.isBuiltin = false;
            auto mk = [](llvm::Type* t, int idx, const std::string& tn) {
                CompilerContext::StructMember m; m.type = t; m.index = idx; m.typeName = tn; return m;
            };
            ci.members["method"]       = mk(i8p, 0, "string");
            ci.members["net_id"]       = mk(i32, 1, "i32");
            ci.members["arg"]          = mk(i32, 2, "i32");
            ci.members["arg_kind"]     = mk(i32, 3, "i32");
            ci.members["arg_blob"]     = mk(i8p, 4, "ptr");
            ci.members["arg_blob_len"] = mk(i32, 5, "i32");
            ci.members["peer_id"]      = mk(i32, 6, "i32");
            ci.members["kind"]         = mk(i32, 7, "i32");   // 0 push, 1 request, 2 response, 3 object_call
            ci.members["request_id"]   = mk(ctx.builder.getInt64Ty(), 8, "i64");

            // (decode is exposed as mirror_session.decode_rpc(ev, target) — see below.)
#if 0
            // ── decode_into(out_var) — decode the typed-RPC blob into a model alloca.
            // The target type is taken from the VariableExpr passed as the first arg
            // via ctx.deserializeTargetType (set in ast_compiler_call.cpp before dispatch).
            // Returns 0 on success, -1 on error (e.g. arg_kind != 1, schema mismatch).
            ci.builtinMethods["decode_into"] = [i8p, i32, vd](CompilerContext& ctx,
                llvm::Value* base, AIML::AST::CallExpr* call, EvalFn evalExpr) -> IRValue* {
                if (call->args.size() < 1) return ctx.ir_gen->getInt32(-1);
                // Resolve the target variable's alloca + its type name.
                llvm::Value* outPtr = nullptr;
                std::string  outTn;
                if (auto* vx = dynamic_cast<AIML::AST::VariableExpr*>(call->args[0].get())) {
                    Symbol* sym = nullptr;
                    CompilerContext::Scope* sc = ctx.currentScope;
                    while (sc) {
                        if (sc->variables.count(vx->name)) { sym = &sc->variables[vx->name]; break; }
                        sc = sc->parent;
                    }
                    if (!sym && ctx.symbol_table.count(vx->name)) sym = &ctx.symbol_table[vx->name];
                    if (!sym) return ctx.ir_gen->getInt32(-1);
                    llvm::Value* av = sym->alloca_inst
                        ? static_cast<llvm::Value*>(sym->alloca_inst)
                        : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(sym->ir_alloca_inst));
                    // AIML v2: a value-model local's slot holds a %M* (pointer to the object), not
                    // the inline struct — load it so we decode INTO the object (and memset the
                    // object, not the 8-byte slot + adjacent stack). v1: the alloca IS the struct.
                    outTn  = sym->typeName;
                    outPtr = ctx.isRefcountModel(outTn)
                        ? ctx.builder.CreateLoad(i8p, av, "decode_modelptr")
                        : ctx.builder.CreateBitCast(av, i8p);
                }
                if (!outPtr || outTn.empty()) return ctx.ir_gen->getInt32(-1);
                if (!ctx.classes.count(outTn) || !ctx.classes[outTn].serNodesVar)
                    return ctx.ir_gen->getInt32(-1);

                auto& ci2 = ctx.classes[outTn];
                // Zero-initialise the target so heap-field pointers start null
                // (aiml_ser_decode docs: caller must zero-init `out`).
                llvm::DataLayout dl(&ctx.module());
                uint64_t outSz = dl.getTypeAllocSize(ci2.type);
                ctx.builder.CreateMemSet(
                    outPtr, ctx.builder.getInt8(0),
                    ctx.builder.getInt64(outSz),
                    llvm::MaybeAlign(1));

                // ev pointer: `base` points to a local rpc_event alloca; we need its address as i8*.
                llvm::Value* evPtr = ctx.builder.CreateBitCast(base, i8p);
                llvm::Value* nodesPtr = ci2.serNodesVar;
                llvm::Value* nNodes   = ctx.builder.getInt32((uint32_t)ci2.serNodesCount);
                llvm::Value* r = ctx.builder.CreateCall(
                    ctx.getFunction("aiml_rpc_event_decode_into",
                        i32, {i8p, i8p, i8p, i32}),
                    {evPtr, outPtr, nodesPtr, nNodes});
                return ctx.ir_gen->wrap(r);
            };
#endif
            ctx.classes["rpc_event"] = std::move(ci);
        }

        // ── mirror_peer (opaque handle, no methods in v1) ─────────────────────
        {
            CI ci; ci.name = "mirror_peer"; ci.isBuiltin = true;
            ctx.classes["mirror_peer"] = std::move(ci);
        }

        // ── mirror_session ────────────────────────────────────────────────────
        // Helper: walk scope chain to find variable and return its alloca as i8*
        // (defined as a normal function pointer / capture, reused in all methods)
#define MIRROR_FIND_OBJ_PTR(call_arg_idx) \
        ([&]() -> llvm::Value* { \
            if (call->args.size() <= (call_arg_idx)) return nullptr; \
            if (auto* _vx = dynamic_cast<AIML::AST::VariableExpr*>(call->args[(call_arg_idx)].get())) { \
                CompilerContext::Scope* _sc = ctx.currentScope; \
                while (_sc) { if (_sc->variables.count(_vx->name)) { \
                    Symbol& _os = _sc->variables[_vx->name]; \
                    llvm::Value* _av = _os.alloca_inst \
                        ? static_cast<llvm::Value*>(_os.alloca_inst) \
                        : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(_os.ir_alloca_inst)); \
                    llvm::Value* _mp = ctx.builder.CreateBitCast(_av, i8p); \
                    if (ctx.isRefcountModel(_os.typeName) || mirrorIsPtrLike(_os.typeName)) _mp = ctx.builder.CreateLoad(i8p, _mp, "mirror_modelptr"); \
                    return _mp; } _sc = _sc->parent; } \
                if (ctx.symbol_table.count(_vx->name)) { \
                    Symbol& _os = ctx.symbol_table[_vx->name]; \
                    llvm::Value* _av = _os.alloca_inst \
                        ? static_cast<llvm::Value*>(_os.alloca_inst) \
                        : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(_os.ir_alloca_inst)); \
                    llvm::Value* _mp = ctx.builder.CreateBitCast(_av, i8p); \
                    if (ctx.isRefcountModel(_os.typeName) || mirrorIsPtrLike(_os.typeName)) _mp = ctx.builder.CreateLoad(i8p, _mp, "mirror_modelptr"); \
                    return _mp; } \
            } \
            IRValue* _av2 = evalExpr(call->args[(call_arg_idx)].get()); \
            if (!_av2) return nullptr; \
            llvm::Value* _v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(_av2)); \
            if (_v->getType()->isPointerTy()) return ctx.builder.CreateBitCast(_v, i8p); \
            /* spec/44_5 §4: the session borrows the object — a temporary would be gone */ \
            LOG_ERROR(ctx.fmtLoc() + "mirror_session needs an addressable object (variable, global, " \
                      "ptr/ref/shared), not a temporary value"); \
            return nullptr; \
        }())

        {
            CI ci; ci.name = "mirror_session"; ci.isBuiltin = true;

            // ── Zero-arg void methods ─────────────────────────────────────────
            auto mkVoid0 = [vd](const char* fn) -> MH {
                return [fn, vd](CompilerContext& ctx, llvm::Value* base,
                    AIML::AST::CallExpr*, EvalFn) -> IRValue* {
                    llvm::Type* p = ctx.builder.getInt8Ty()->getPointerTo();
                    llvm::Value* s = ctx.builder.CreateLoad(p, base, "sess_ptr");
                    ctx.builder.CreateCall(ctx.getFunction(fn, vd, {p}), {s});
                    return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
                };
            };
            auto mkI320 = [i32](const char* fn) -> MH {
                return [fn, i32](CompilerContext& ctx, llvm::Value* base,
                    AIML::AST::CallExpr*, EvalFn) -> IRValue* {
                    llvm::Type* p = ctx.builder.getInt8Ty()->getPointerTo();
                    llvm::Value* s = ctx.builder.CreateLoad(p, base, "sess_ptr");
                    return ctx.ir_gen->wrap(
                        ctx.builder.CreateCall(ctx.getFunction(fn, i32, {p}), {s}));
                };
            };

            ci.builtinMethods["close"]        = mkVoid0("aiml_mirror_session_close");
            ci.builtinMethods["update"]       = mkVoid0("aiml_mirror_session_update");
            ci.builtinMethods["is_connected"] = mkI320("aiml_mirror_session_is_connected");
            ci.builtinMethods["peer_count"]   = mkI320("aiml_mirror_session_peer_count");
            ci.builtinMethods["has_event"]    = mkI320("aiml_mirror_session_has_event");

            // spec/44_5 §4 readiness: snapshot_revision(net_id) → u64, snapshot_error(net_id) → i32
            auto mkSnapshot = [i8p, i32](const char* fn, const char* name, llvm::Type* retTy) -> MH {
                return [fn, name, i8p, i32, retTy](CompilerContext& ctx, llvm::Value* base,
                    AIML::AST::CallExpr* call, EvalFn evalExpr) -> IRValue* {
                    if (!call || call->args.size() != 1) {
                        LOG_ERROR(ctx.fmtLoc() + "mirror_session." + name + " expects one argument: net_id (i32)");
                        return ctx.ir_gen->wrap(llvm::ConstantInt::get(retTy, 0));
                    }
                    llvm::Value* id = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
                    if (!id || !id->getType()->isIntegerTy()) {
                        LOG_ERROR(ctx.fmtLoc() + "mirror_session." + name + ": net_id must be an integer");
                        return ctx.ir_gen->wrap(llvm::ConstantInt::get(retTy, 0));
                    }
                    if (!id->getType()->isIntegerTy(32)) id = ctx.builder.CreateIntCast(id, i32, true);
                    llvm::Value* s = ctx.builder.CreateLoad(i8p, base, "sess_ptr");
                    return ctx.ir_gen->wrap(ctx.builder.CreateCall(ctx.getFunction(fn, retTy, {i8p, i32}), {s, id}));
                };
            };
            ci.builtinMethods["snapshot_revision"] = mkSnapshot("aiml_mirror_session_snapshot_revision", "snapshot_revision", i64);
            ci.builtinMethods["snapshot_error"]    = mkSnapshot("aiml_mirror_session_snapshot_error", "snapshot_error", i32);

            // session.peer_ip(peer_id) → string  (dotted IP of a peer, captured at accept)
            ci.builtinMethods["peer_ip"] = [i8p, i32](CompilerContext& ctx, llvm::Value* base,
                AIML::AST::CallExpr* call, EvalFn evalExpr) -> IRValue* {
                llvm::Value* s = ctx.builder.CreateLoad(i8p, base, "sess_ptr");
                llvm::Value* pid = (call && !call->args.empty())
                    ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())))
                    : ctx.builder.getInt32(0);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction("aiml_mirror_peer_ip", i8p, {i8p, i32}), {s, pid}));
            };

            // ── register(obj) → i32 (net_id) ──────────────────────────────────
            ci.builtinMethods["register"] = [i8p, i32, i64, vd](CompilerContext& ctx,
                llvm::Value* base, AIML::AST::CallExpr* call, EvalFn evalExpr) -> IRValue* {
                llvm::Value* s = ctx.builder.CreateLoad(i8p, base, "sess_ptr");
                llvm::Value* objPtr = MIRROR_FIND_OBJ_PTR(0);
                if (!objPtr) return ctx.ir_gen->getInt32(-1);

                int64_t objSize = 8;
                const std::string tn = mirrorInnerType(ctx.deserializeTargetType);
                if (!tn.empty() && ctx.classes.count(tn) && ctx.classes[tn].type) {
                    llvm::DataLayout dl(&ctx.module());
                    objSize = (int64_t)dl.getTypeAllocSize(ctx.classes[tn].type);
                }
                // (v2 %M* deref handled in MIRROR_FIND_OBJ_PTR — objPtr is the object.)
                // spec/44_5 §10: every serializable model travels as DELTA format 2.
                if (!mirrorSyncableType(ctx, tn)) return ctx.ir_gen->getInt32(-1);
                if (!tn.empty() && ctx.classes.count(tn) && ctx.classes[tn].serNodesVar) {
                    auto& ci2 = ctx.classes[tn];
                    llvm::Value* nodesPtr = ci2.serNodesVar;
                    llvm::Value* nNodes   = ctx.builder.getInt32((uint32_t)ci2.serNodesCount);
                    llvm::Value* nidT = ctx.builder.CreateCall(
                        ctx.getFunction("aiml_mirror_session_register_typed",
                            i32, {i8p, i8p, i64, i8p, i32}),
                        {s, objPtr, ctx.builder.getInt64(objSize), nodesPtr, nNodes});
                    emitRpcTableRegistration(ctx, s, objPtr, tn);
                    return ctx.ir_gen->wrap(nidT);
                }
                llvm::Value* nidU = ctx.builder.CreateCall(
                    ctx.getFunction("aiml_mirror_session_register", i32, {i8p, i8p, i64}),
                    {s, objPtr, ctx.builder.getInt64(objSize)});
                if (!tn.empty()) emitRpcTableRegistration(ctx, s, objPtr, tn);
                return ctx.ir_gen->wrap(nidU);
            };

            // ── register_id(obj, id: i32) → void ─────────────────────────────
            ci.builtinMethods["register_id"] = [i8p, i32, i64, vd](CompilerContext& ctx,
                llvm::Value* base, AIML::AST::CallExpr* call, EvalFn evalExpr) -> IRValue* {
                llvm::Value* s = ctx.builder.CreateLoad(i8p, base, "sess_ptr");
                llvm::Value* objPtr = MIRROR_FIND_OBJ_PTR(0);
                llvm::Value* idVal = ctx.builder.getInt32(0);
                if (call->args.size() >= 2) {
                    IRValue* idIRV = evalExpr(call->args[1].get());
                    if (idIRV) idVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(idIRV));
                }
                if (!objPtr) return ctx.ir_gen->wrap(ctx.builder.getInt32(0));

                int64_t objSize = 8;
                const std::string tn = mirrorInnerType(ctx.deserializeTargetType);
                if (!tn.empty() && ctx.classes.count(tn) && ctx.classes[tn].type) {
                    llvm::DataLayout dl(&ctx.module());
                    objSize = (int64_t)dl.getTypeAllocSize(ctx.classes[tn].type);
                }
                // (v2 %M* deref handled in MIRROR_FIND_OBJ_PTR — objPtr is the object.)
                // spec/44_5 §10: every serializable model travels as DELTA format 2.
                if (!mirrorSyncableType(ctx, tn)) return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
                if (!tn.empty() && ctx.classes.count(tn) && ctx.classes[tn].serNodesVar) {
                    auto& ci2 = ctx.classes[tn];
                    llvm::Value* nodesPtr = ci2.serNodesVar;
                    llvm::Value* nNodes   = ctx.builder.getInt32((uint32_t)ci2.serNodesCount);
                    ctx.builder.CreateCall(
                        ctx.getFunction("aiml_mirror_session_register_id_typed",
                            vd, {i8p, i8p, i64, i32, i8p, i32}),
                        {s, objPtr, ctx.builder.getInt64(objSize), idVal, nodesPtr, nNodes});
                    emitRpcTableRegistration(ctx, s, objPtr, tn);
                    return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
                }
                ctx.builder.CreateCall(
                    ctx.getFunction("aiml_mirror_session_register_id", vd, {i8p, i8p, i64, i32}),
                    {s, objPtr, ctx.builder.getInt64(objSize), idVal});
                if (!tn.empty()) emitRpcTableRegistration(ctx, s, objPtr, tn);
                return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
            };

            // ── unregister(obj) → void ────────────────────────────────────────
            ci.builtinMethods["unregister"] = [i8p, vd](CompilerContext& ctx,
                llvm::Value* base, AIML::AST::CallExpr* call, EvalFn evalExpr) -> IRValue* {
                llvm::Value* s = ctx.builder.CreateLoad(i8p, base, "sess_ptr");
                llvm::Value* objPtr = MIRROR_FIND_OBJ_PTR(0);
                if (!objPtr) return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
                ctx.builder.CreateCall(
                    ctx.getFunction("aiml_mirror_session_unregister", vd, {i8p, i8p}),
                    {s, objPtr});
                return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
            };

            // ── has_rpc() → i32 ───────────────────────────────────────────────
            ci.builtinMethods["has_rpc"] = mkI320("aiml_mirror_session_has_rpc");

            // ── next_rpc() → rpc_event (struct loaded from heap ptr) ──────────
            ci.builtinMethods["next_rpc"] = [i8p](CompilerContext& ctx,
                llvm::Value* base, AIML::AST::CallExpr*, EvalFn) -> IRValue* {
                llvm::Value* s = ctx.builder.CreateLoad(i8p, base, "sess_ptr");
                llvm::Value* evPtr = ctx.builder.CreateCall(
                    ctx.getFunction("aiml_mirror_session_next_rpc", i8p, {i8p}), {s}, "rpc_ptr");
                llvm::StructType* evTy = static_cast<llvm::StructType*>(ctx.getLLVMType("rpc_event"));
                llvm::AllocaInst* evAlloca = ctx.createEntryAlloca(evTy, "rpc_tmp");
                llvm::DataLayout dl(&ctx.module());
                uint64_t szBytes = dl.getTypeAllocSize(evTy);
                llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
                ctx.builder.CreateCall(
                    ctx.getFunction("memcpy", ptrTy, {ptrTy, ptrTy, ctx.getSizeType()}),
                    {evAlloca, evPtr, ctx.getSizeConst(szBytes)});
                return ctx.ir_gen->wrap(evAlloca);
            };

            // ── send_rpc(net_id: i32, method: string, arg: i32 [, model_obj]) → void
            // If a 4th argument is supplied AND it is a VariableExpr referring to a
            // user model, the typed payload path is taken: the model is encoded via
            // aiml_ser_encode and shipped alongside the i32 discriminator.
            ci.builtinMethods["send_rpc"] = [i8p, i32, i64, vd](CompilerContext& ctx,
                llvm::Value* base, AIML::AST::CallExpr* call, EvalFn evalExpr) -> IRValue* {
                llvm::Value* s = ctx.builder.CreateLoad(i8p, base, "sess_ptr");
                if (call->args.size() < 3) return ctx.ir_gen->getInt32(0);
                llvm::Value* nid    = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
                llvm::Value* method = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
                llvm::Value* arg    = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[2].get())));

                // Optional typed-payload path: 4th arg = model variable
                if (call->args.size() >= 4) {
                    if (auto* vx = dynamic_cast<AIML::AST::VariableExpr*>(call->args[3].get())) {
                        Symbol* sym = nullptr;
                        CompilerContext::Scope* sc = ctx.currentScope;
                        while (sc) {
                            if (sc->variables.count(vx->name)) { sym = &sc->variables[vx->name]; break; }
                            sc = sc->parent;
                        }
                        if (!sym && ctx.symbol_table.count(vx->name)) sym = &ctx.symbol_table[vx->name];
                        if (sym) {
                            // Strip ptr<X> → X so that pointer-typed model args also work
                            std::string tn = sym->typeName;
                            bool isPtr = (tn.size() > 4 && tn.substr(0,4) == "ptr<" && tn.back() == '>');
                            if (isPtr) tn = tn.substr(4, tn.size()-5);
                            if (ctx.classes.count(tn) && ctx.classes[tn].serNodesVar) {
                                auto& ci2 = ctx.classes[tn];
                            llvm::Value* objAv = sym->alloca_inst
                                ? static_cast<llvm::Value*>(sym->alloca_inst)
                                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(sym->ir_alloca_inst));
                            // The alloca holds a POINTER to the struct when the arg is a ptr<X> OR,
                            // under AIML v2, a value-model whose local slot holds a %M* — load it.
                            // Otherwise (v1 inline value struct) the alloca IS the struct.
                            llvm::Value* objPtr;
                            if (isPtr || ctx.isRefcountModel(tn)) {
                                objPtr = ctx.builder.CreateLoad(i8p, objAv, "pay_ptr");
                            } else {
                                objPtr = ctx.builder.CreateBitCast(objAv, i8p);
                            }
                            llvm::Value* nodesPtr = ci2.serNodesVar;
                            llvm::Value* nNodes   = ctx.builder.getInt32((uint32_t)ci2.serNodesCount);
                            ctx.builder.CreateCall(
                                ctx.getFunction("aiml_mirror_session_send_rpc_model",
                                    vd, {i8p, i32, i8p, i32, i8p, i8p, i32}),
                                {s, nid, method, arg, objPtr, nodesPtr, nNodes});
                                return ctx.ir_gen->getInt32(0);
                            } else {
                                // 4th arg present but type is not a serializable model → compiler error
                                LOG_ERROR(ctx.fmtLoc() + "send_rpc: 4th argument '" + vx->name +
                                    "' (type '" + sym->typeName + "') is not a serializable model type");
                            }
                        } // if sym
                    }
                }

                ctx.builder.CreateCall(
                    ctx.getFunction("aiml_mirror_session_send_rpc", vd, {i8p, i32, i8p, i32}),
                    {s, nid, method, arg});
                return ctx.ir_gen->getInt32(0);
            };

            // ── decode_rpc(ev: rpc_event, target: <Model>) → i32
            // Decode the typed-RPC blob carried by `ev` into the caller-supplied
            // model variable. Returns 0 on success, -1 if ev.arg_kind != 1 or
            // schema mismatch. Heap fields inside `target` (strings/lists/maps)
            // are newly allocated by aiml_ser_decode and must be released by
            // the caller via the model lifetime (currently leaked — see stage 1).
            // decode_rpc(ev, target) → i32: transactional (spec/44_5) — a failed decode leaves the
            // target untouched; success releases its previous heap.  0 = ok, -1 = wrong type/error.
            ci.builtinMethods["decode_rpc"] = [i8p, i32](CompilerContext& ctx,
                llvm::Value* /*base*/, AIML::AST::CallExpr* call, EvalFn) -> IRValue* {
                if (call->args.size() < 2) return ctx.ir_gen->getInt32(-1);
                llvm::Value* evPtr = rpcEventArg(ctx, call->args[0].get());
                RpcModelArg m;
                if (!evPtr || !rpcModelArg(ctx, call->args[1].get(), m)) {
                    LOG_ERROR(ctx.fmtLoc() + "decode_rpc: expected (rpc_event variable, serializable model variable)");
                    return ctx.ir_gen->getInt32(-1);
                }
                llvm::Value* r = ctx.builder.CreateCall(
                    ctx.getFunction("aiml_rpc_event_decode_tx", i32, {i8p, i8p, ctx.builder.getInt64Ty(), i8p, i32}),
                    {evPtr, m.ptr, ctx.builder.getInt64(m.size), m.nodes, m.n});
                return ctx.ir_gen->wrap(r);
            };

            // ── take_rpc(method, target) → i32 (spec/44_5): first queued push of `method` whose
            // payload is of the target's type; others keep their position.  1 = taken, 0 = none.
            ci.builtinMethods["take_rpc"] = [i8p, i32](CompilerContext& ctx,
                llvm::Value* base, AIML::AST::CallExpr* call, EvalFn evalExpr) -> IRValue* {
                if (call->args.size() < 2) return ctx.ir_gen->getInt32(0);
                llvm::Value* s = ctx.builder.CreateLoad(i8p, base, "sess_ptr");
                llvm::Value* method = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
                RpcModelArg m;
                if (!rpcModelArg(ctx, call->args[1].get(), m)) {
                    LOG_ERROR(ctx.fmtLoc() + "take_rpc: second argument must be a serializable model variable");
                    return ctx.ir_gen->getInt32(0);
                }
                llvm::Value* r = ctx.builder.CreateCall(
                    ctx.getFunction("aiml_mirror_session_take_rpc", i32, {i8p, i8p, i8p, ctx.builder.getInt64Ty(), i8p, i32}),
                    {s, method, m.ptr, ctx.builder.getInt64(m.size), m.nodes, m.n});
                return ctx.ir_gen->wrap(r);
            };

            // ── reply_rpc(ev, method, arg[, model]) (spec/44_5): answer the request in `ev` — to its
            // sender peer, echoing its request id (a push when `ev` was not a request).
            ci.builtinMethods["reply_rpc"] = [i8p, i32, vd](CompilerContext& ctx,
                llvm::Value* base, AIML::AST::CallExpr* call, EvalFn evalExpr) -> IRValue* {
                if (call->args.size() < 3) {
                    LOG_ERROR(ctx.fmtLoc() + "reply_rpc: expected (rpc_event, method, arg[, model])");
                    return ctx.ir_gen->getInt32(0);
                }
                llvm::Value* s = ctx.builder.CreateLoad(i8p, base, "sess_ptr");
                llvm::Value* evPtr = rpcEventArg(ctx, call->args[0].get());
                if (!evPtr) {
                    LOG_ERROR(ctx.fmtLoc() + "reply_rpc: first argument must be the rpc_event variable");
                    return ctx.ir_gen->getInt32(0);
                }
                llvm::Value* method = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
                llvm::Value* arg    = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[2].get())));
                if (arg->getType() != i32 && arg->getType()->isIntegerTy()) arg = ctx.builder.CreateIntCast(arg, i32, true);
                llvm::Value* nullp = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p));
                llvm::Value *obj = nullp, *nodes = nullp, *n = ctx.builder.getInt32(0);
                if (call->args.size() >= 4) {
                    RpcModelArg m;
                    if (!rpcModelArg(ctx, call->args[3].get(), m)) {
                        LOG_ERROR(ctx.fmtLoc() + "reply_rpc: 4th argument must be a serializable model variable");
                        return ctx.ir_gen->getInt32(0);
                    }
                    obj = m.ptr; nodes = m.nodes; n = m.n;
                }
                ctx.builder.CreateCall(
                    ctx.getFunction("aiml_mirror_session_reply_rpc", vd, {i8p, i8p, i8p, i32, i8p, i8p, i32}),
                    {s, evPtr, method, arg, obj, nodes, n});
                return ctx.ir_gen->getInt32(0);
            };

            // ── send_request(target, method, arg, model) → i64 request id (0 = not sent) ──
            ci.builtinMethods["send_request"] = [i8p, i32](CompilerContext& ctx,
                llvm::Value* base, AIML::AST::CallExpr* call, EvalFn evalExpr) -> IRValue* {
                llvm::Type* i64 = ctx.builder.getInt64Ty();
                if (call->args.size() < 4) {
                    LOG_ERROR(ctx.fmtLoc() + "send_request: expected (target, method, arg, model)");
                    return ctx.ir_gen->wrap(ctx.builder.getInt64(0));
                }
                llvm::Value* s = ctx.builder.CreateLoad(i8p, base, "sess_ptr");
                llvm::Value* target = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
                llvm::Value* method = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
                llvm::Value* arg    = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[2].get())));
                if (target->getType() != i32 && target->getType()->isIntegerTy()) target = ctx.builder.CreateIntCast(target, i32, true);
                if (arg->getType() != i32 && arg->getType()->isIntegerTy()) arg = ctx.builder.CreateIntCast(arg, i32, true);
                RpcModelArg m;
                if (!rpcModelArg(ctx, call->args[3].get(), m)) {
                    LOG_ERROR(ctx.fmtLoc() + "send_request: 4th argument must be a serializable model variable");
                    return ctx.ir_gen->wrap(ctx.builder.getInt64(0));
                }
                llvm::Value* r = ctx.builder.CreateCall(
                    ctx.getFunction("aiml_mirror_session_send_request", i64, {i8p, i32, i8p, i32, i8p, i8p, i32}),
                    {s, target, method, arg, m.ptr, m.nodes, m.n});
                return ctx.ir_gen->wrap(r);
            };

            // ── take_response(id, target) → i32: 1 = decoded, 0 = not yet, -1 = protocol error ──
            ci.builtinMethods["take_response"] = [i8p, i32](CompilerContext& ctx,
                llvm::Value* base, AIML::AST::CallExpr* call, EvalFn evalExpr) -> IRValue* {
                llvm::Type* i64 = ctx.builder.getInt64Ty();
                if (call->args.size() < 2) return ctx.ir_gen->getInt32(-1);
                llvm::Value* s  = ctx.builder.CreateLoad(i8p, base, "sess_ptr");
                llvm::Value* id = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
                if (id->getType() != i64 && id->getType()->isIntegerTy()) id = ctx.builder.CreateIntCast(id, i64, true);
                RpcModelArg m;
                if (!rpcModelArg(ctx, call->args[1].get(), m)) {
                    LOG_ERROR(ctx.fmtLoc() + "take_response: second argument must be a serializable model variable");
                    return ctx.ir_gen->getInt32(-1);
                }
                llvm::Value* r = ctx.builder.CreateCall(
                    ctx.getFunction("aiml_mirror_session_take_response", i32, {i8p, i64, i8p, i8p, i64, i8p, i32}),
                    {s, id, ctx.builder.CreateGlobalStringPtr("set_result"), m.ptr, ctx.builder.getInt64(m.size), m.nodes, m.n});
                return ctx.ir_gen->wrap(r);
            };

            // ── cancel_request(id): drop interest (a late response is then discarded) ──
            ci.builtinMethods["cancel_request"] = [i8p, vd](CompilerContext& ctx,
                llvm::Value* base, AIML::AST::CallExpr* call, EvalFn evalExpr) -> IRValue* {
                llvm::Type* i64 = ctx.builder.getInt64Ty();
                if (call->args.size() < 1) return ctx.ir_gen->getInt32(0);
                llvm::Value* s  = ctx.builder.CreateLoad(i8p, base, "sess_ptr");
                llvm::Value* id = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
                if (id->getType() != i64 && id->getType()->isIntegerTy()) id = ctx.builder.CreateIntCast(id, i64, true);
                ctx.builder.CreateCall(ctx.getFunction("aiml_mirror_session_cancel_request", vd, {i8p, i64}), {s, id});
                return ctx.ir_gen->getInt32(0);
            };

            // ── send_and_await(target_peer, method, req, out, timeout_ms) → i32
            //
            // One-shot RPC roundtrip: encode `req` (typed model), send to
            // `target_peer` with method name, then poll the incoming queue
            // for `timeout_ms` waiting for an RPC with method=="set_result";
            // on match decode into `out` (typed ptr<Model>) and return 1.
            // Return 0 on timeout / decode failure.
            //
            // Replaces the hand-rolled per-Type await-loop pattern that
            // client-side tests had to repeat — see 9rooms_v2 MockClient
            // refactor for the motivating use-case.
            //
            // Both `req` and `out` MUST be model-typed VariableExpr — the
            // builtin extracts schema-nodes from the registered class info.
            // `out` may be `ptr<Model>` or plain `Model` (ref/ptr identical
            // today — same convention as decode_rpc).
            ci.builtinMethods["send_and_await"] = [i8p, i32, vd](CompilerContext& ctx,
                llvm::Value* base, AIML::AST::CallExpr* call, EvalFn evalExpr) -> IRValue* {
                if (call->args.size() < 5) {
                    LOG_ERROR(ctx.fmtLoc() + "send_and_await: expected (target_peer, method, req, out, timeout_ms) — got " +
                              std::to_string(call->args.size()) + " args");
                    return ctx.ir_gen->getInt32(0);
                }
                llvm::Value* s        = ctx.builder.CreateLoad(i8p, base, "sess_ptr");
                llvm::Value* targetId = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
                llvm::Value* method   = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
                RpcModelArg req, out;
                if (!rpcModelArg(ctx, call->args[2].get(), req)) {
                    LOG_ERROR(ctx.fmtLoc() + "send_and_await: 3rd arg must be a serializable model variable");
                    return ctx.ir_gen->getInt32(0);
                }
                // The out model is decoded transactionally (spec/44_5): no pre-zeroing here — a
                // failed/absent reply leaves it untouched, a reply replaces its previous heap.
                if (!rpcModelArg(ctx, call->args[3].get(), out)) {
                    LOG_ERROR(ctx.fmtLoc() + "send_and_await: 4th arg must be a serializable model variable (typically ptr<Model>)");
                    return ctx.ir_gen->getInt32(0);
                }
                llvm::Value* timeoutMs = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[4].get())));
                llvm::Value* expectMethod = ctx.builder.CreateGlobalStringPtr("set_result");
                llvm::Type* i64 = ctx.builder.getInt64Ty();
                llvm::Value* r = ctx.builder.CreateCall(
                    ctx.getFunction("aiml_mirror_session_send_and_await",
                        i32, {i8p, i32, i8p, i32, i8p, i8p, i32, i8p, i8p, i64, i8p, i32, i32}),
                    {s, targetId, method, ctx.builder.getInt32(0),
                     req.ptr, req.nodes, req.n,
                     expectMethod,
                     out.ptr, ctx.builder.getInt64(out.size), out.nodes, out.n,
                     timeoutMs});
                return ctx.ir_gen->wrap(r);
            };

            // ── next_event() → mirror_event (struct loaded from heap ptr) ─────
            ci.builtinMethods["next_event"] = [i8p](CompilerContext& ctx,
                llvm::Value* base, AIML::AST::CallExpr*, EvalFn) -> IRValue* {
                llvm::Value* s = ctx.builder.CreateLoad(i8p, base, "sess_ptr");
                llvm::Value* evPtr = ctx.builder.CreateCall(
                    ctx.getFunction("aiml_mirror_session_next_event", i8p, {i8p}), {s}, "ev_ptr");
                // Allocate local, memcpy struct from heap pointer.
                // visitSetStmt (isBuiltin=false, ptr val) will memcpy into target alloca.
                llvm::StructType* evTy = static_cast<llvm::StructType*>(ctx.getLLVMType("mirror_event"));
                llvm::AllocaInst* evAlloca = ctx.createEntryAlloca(evTy, "ev_tmp");
                llvm::DataLayout dl(&ctx.module());
                uint64_t szBytes = dl.getTypeAllocSize(evTy);
                llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
                ctx.builder.CreateCall(
                    ctx.getFunction("memcpy", ptrTy, {ptrTy, ptrTy, ctx.getSizeType()}),
                    {evAlloca, evPtr, ctx.getSizeConst(szBytes)});
                return ctx.ir_gen->wrap(evAlloca);
            };

            // ── poll_event() → mirror_event (empty sentinel type=="" if queue empty) ──
            ci.builtinMethods["poll_event"] = [i8p](CompilerContext& ctx,
                llvm::Value* base, AIML::AST::CallExpr*, EvalFn) -> IRValue* {
                llvm::Value* s = ctx.builder.CreateLoad(i8p, base, "sess_ptr");
                llvm::Value* evPtr = ctx.builder.CreateCall(
                    ctx.getFunction("aiml_mirror_session_poll_event", i8p, {i8p}), {s}, "poll_ev_ptr");
                llvm::StructType* evTy = static_cast<llvm::StructType*>(ctx.getLLVMType("mirror_event"));
                llvm::AllocaInst* evAlloca = ctx.createEntryAlloca(evTy, "poll_ev_tmp");
                llvm::DataLayout dl(&ctx.module());
                uint64_t szBytes = dl.getTypeAllocSize(evTy);
                llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
                ctx.builder.CreateCall(
                    ctx.getFunction("memcpy", ptrTy, {ptrTy, ptrTy, ctx.getSizeType()}),
                    {evAlloca, evPtr, ctx.getSizeConst(szBytes)});
                return ctx.ir_gen->wrap(evAlloca);
            };

            // ── poll_rpc() → rpc_event (empty sentinel method=="" if queue empty) ──
            ci.builtinMethods["poll_rpc"] = [i8p](CompilerContext& ctx,
                llvm::Value* base, AIML::AST::CallExpr*, EvalFn) -> IRValue* {
                llvm::Value* s = ctx.builder.CreateLoad(i8p, base, "sess_ptr");
                llvm::Value* evPtr = ctx.builder.CreateCall(
                    ctx.getFunction("aiml_mirror_session_poll_rpc", i8p, {i8p}), {s}, "poll_rpc_ptr");
                llvm::StructType* evTy = static_cast<llvm::StructType*>(ctx.getLLVMType("rpc_event"));
                llvm::AllocaInst* evAlloca = ctx.createEntryAlloca(evTy, "poll_rpc_tmp");
                llvm::DataLayout dl(&ctx.module());
                uint64_t szBytes = dl.getTypeAllocSize(evTy);
                llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);
                ctx.builder.CreateCall(
                    ctx.getFunction("memcpy", ptrTy, {ptrTy, ptrTy, ctx.getSizeType()}),
                    {evAlloca, evPtr, ctx.getSizeConst(szBytes)});
                return ctx.ir_gen->wrap(evAlloca);
            };

            // ── set_sync_rate(obj, hz: f32) ───────────────────────────────────
            ci.builtinMethods["set_sync_rate"] = [i8p, f32, vd](CompilerContext& ctx,
                llvm::Value* base, AIML::AST::CallExpr* call, EvalFn evalExpr) -> IRValue* {
                llvm::Value* s = ctx.builder.CreateLoad(i8p, base, "sess_ptr");
                llvm::Value* objPtr = MIRROR_FIND_OBJ_PTR(0);
                llvm::Value* hzVal = call->args.size() >= 2
                    ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())))
                    : llvm::ConstantFP::get(f32, 0.0);
                if (!hzVal->getType()->isFloatTy()) hzVal = ctx.builder.CreateFPCast(hzVal, f32);
                if (!objPtr) return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
                ctx.builder.CreateCall(
                    ctx.getFunction("aiml_mirror_session_set_sync_rate", vd, {i8p, i8p, f32}),
                    {s, objPtr, hzVal});
                return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
            };

            // ── sync_now(obj) ─────────────────────────────────────────────────
            ci.builtinMethods["sync_now"] = [i8p, vd](CompilerContext& ctx,
                llvm::Value* base, AIML::AST::CallExpr* call, EvalFn evalExpr) -> IRValue* {
                llvm::Value* s = ctx.builder.CreateLoad(i8p, base, "sess_ptr");
                llvm::Value* objPtr = MIRROR_FIND_OBJ_PTR(0);
                if (!objPtr) return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
                ctx.builder.CreateCall(
                    ctx.getFunction("aiml_mirror_session_sync_now", vd, {i8p, i8p}),
                    {s, objPtr});
                return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
            };

            // ── set_sync_mode(obj, mode: string) ─────────────────────────────
            ci.builtinMethods["set_sync_mode"] = [i8p, vd](CompilerContext& ctx,
                llvm::Value* base, AIML::AST::CallExpr* call, EvalFn evalExpr) -> IRValue* {
                llvm::Value* s = ctx.builder.CreateLoad(i8p, base, "sess_ptr");
                llvm::Value* objPtr = MIRROR_FIND_OBJ_PTR(0);
                llvm::Value* modeStr = call->args.size() >= 2
                    ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())))
                    : ctx.builder.CreateGlobalStringPtr("auto");
                if (!objPtr) return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
                ctx.builder.CreateCall(
                    ctx.getFunction("aiml_mirror_session_set_sync_mode", vd, {i8p, i8p, i8p}),
                    {s, objPtr, modeStr});
                return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
            };

            // ── set_rpc_target(obj, method: string, target: string) ───────────
            ci.builtinMethods["set_rpc_target"] = [i8p, vd](CompilerContext& ctx,
                llvm::Value* base, AIML::AST::CallExpr* call, EvalFn evalExpr) -> IRValue* {
                llvm::Value* s = ctx.builder.CreateLoad(i8p, base, "sess_ptr");
                llvm::Value* objPtr = MIRROR_FIND_OBJ_PTR(0);
                llvm::Value* mStr = call->args.size() >= 2
                    ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())))
                    : ctx.builder.CreateGlobalStringPtr("");
                llvm::Value* tStr = call->args.size() >= 3
                    ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[2].get())))
                    : ctx.builder.CreateGlobalStringPtr("");
                if (!objPtr) return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
                ctx.builder.CreateCall(
                    ctx.getFunction("aiml_mirror_session_set_rpc_target", vd, {i8p, i8p, i8p, i8p}),
                    {s, objPtr, mStr, tStr});
                return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
            };

            ctx.classes["mirror_session"] = std::move(ci);
        }
#undef MIRROR_FIND_OBJ_PTR

        // ── mirror namespace (factory functions) ──────────────────────────────
        {
            auto& mir = ctx.namespaces["mirror"];

            // mirror.object_call(obj, method, &args) → i32 (spec/44_5 §7.1, used by synthesized rpc
            // wrappers): 1 = routed to other peers only, 0 = run the method body locally.
            mir.builtinFuncs["object_call"] = [i8p](CompilerContext& ctx,
                const std::vector<IRValue*>& args) -> IRValue* {
                if (args.size() < 3) return ctx.ir_gen->getInt32(0);
                llvm::Value* obj = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                llvm::Value* m   = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
                llvm::Value* a   = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]));
                if (!obj || !obj->getType()->isPointerTy() || !a || !a->getType()->isPointerTy())
                    return ctx.ir_gen->getInt32(0);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction("aiml_mirror_object_call", ctx.builder.getInt32Ty(), {i8p, i8p, i8p}),
                    {obj, m, a}));
            };

            mir.builtinFuncs["listen"] = [i8p](CompilerContext& ctx,
                const std::vector<IRValue*>& args) -> IRValue* {
                llvm::Value* url = args.empty()
                    ? ctx.builder.CreateGlobalStringPtr("tcp://0.0.0.0:9000")
                    : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction("aiml_mirror_listen", i8p, {i8p}), {url}));
            };
            mir.builtinFuncs["listen_max"] = [i8p](CompilerContext& ctx,
                const std::vector<IRValue*>& args) -> IRValue* {
                llvm::Type* i32 = ctx.builder.getInt32Ty();
                llvm::Value* url = args.empty()
                    ? ctx.builder.CreateGlobalStringPtr("tcp://0.0.0.0:9000")
                    : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                llvm::Value* mx = args.size() >= 2
                    ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
                    : ctx.builder.getInt32(8);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction("aiml_mirror_listen_max", i8p, {i8p, i32}), {url, mx}));
            };
            // mirror.listen_tls(url, cert, key) — wss:// host with a server cert
            mir.builtinFuncs["listen_tls"] = [i8p](CompilerContext& ctx,
                const std::vector<IRValue*>& args) -> IRValue* {
                llvm::Value* url = args.empty()
                    ? ctx.builder.CreateGlobalStringPtr("wss://0.0.0.0:9443")
                    : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                llvm::Value* cert = args.size() >= 2
                    ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
                    : llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p));
                llvm::Value* key = args.size() >= 3
                    ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]))
                    : llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p));
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction("aiml_mirror_listen_tls", i8p, {i8p, i8p, i8p}), {url, cert, key}));
            };
            // mirror.tls_insecure(on) — disable wss server-cert verification (dev only)
            mir.builtinFuncs["tls_insecure"] = [](CompilerContext& ctx,
                const std::vector<IRValue*>& args) -> IRValue* {
                llvm::Type* i32t = ctx.builder.getInt32Ty();
                llvm::Value* on = args.empty() ? ctx.builder.getInt32(1)
                    : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                ctx.builder.CreateCall(
                    ctx.getFunction("aiml_ws_set_client_insecure", ctx.builder.getVoidTy(), {i32t}), {on});
                return ctx.ir_gen->wrap(ctx.builder.getInt32(0));
            };
            mir.builtinFuncs["connect"] = [i8p](CompilerContext& ctx,
                const std::vector<IRValue*>& args) -> IRValue* {
                llvm::Value* url = args.empty()
                    ? ctx.builder.CreateGlobalStringPtr("tcp://127.0.0.1:9000")
                    : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction("aiml_mirror_connect", i8p, {i8p}), {url}));
            };
            mir.builtinFuncs["connect_timeout"] = [i8p](CompilerContext& ctx,
                const std::vector<IRValue*>& args) -> IRValue* {
                llvm::Type* i32 = ctx.builder.getInt32Ty();
                llvm::Value* url = args.empty()
                    ? ctx.builder.CreateGlobalStringPtr("tcp://127.0.0.1:9000")
                    : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                llvm::Value* ms = args.size() >= 2
                    ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
                    : ctx.builder.getInt32(2000);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(
                    ctx.getFunction("aiml_mirror_connect_timeout", i8p, {i8p, i32}), {url, ms}));
            };
        }

        (void)i64; (void)f32; (void)vd;
    }
    // ── end mirror ─────────────────────────────────────────────────────────────

    // -----------------------------------------------------------------------
    // log namespace (Spec 46)
    // -----------------------------------------------------------------------
    {
        auto& log = ctx.namespaces["log"];
        auto* p = llvm::PointerType::get(ctx.context, 0);
        auto* vd = ctx.builder.getVoidTy();

        /* Macro: register a log level function.
         * The handler expects 2 args: [msg, loc] — loc is injected by the compiler
         * (ast_compiler_call.cpp) before dispatch. If loc is missing, use "?".
         * NOTE: getOrInsert the LLVM function inside the handler (using the
         * handler's own ctx), NOT via a captured lambda from registration scope
         * — capturing the registration-time lambda by value still holds dangling
         * references to the registration-scope ctx/builder. */
#define REG_LOG(aimlName, cName) \
        log.builtinFuncs[aimlName] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* { \
            auto* p = llvm::PointerType::get(ctx.context, 0); \
            auto* vd = ctx.builder.getVoidTy(); \
            llvm::Function* fn = ctx.module().getFunction(cName); \
            if (!fn) \
                fn = llvm::Function::Create( \
                    llvm::FunctionType::get(vd, {p, p}, false), \
                    llvm::Function::ExternalLinkage, cName, ctx.module()); \
            llvm::Value* msg = args.size() > 0 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])) \
                                                : ctx.builder.CreateGlobalStringPtr(""); \
            llvm::Value* loc = args.size() > 1 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])) \
                                                : ctx.builder.CreateGlobalStringPtr("?"); \
            ctx.builder.CreateCall(fn, {msg, loc}); \
            return nullptr; \
        };

        REG_LOG("verbose",    "aiml_log_verbose")
        REG_LOG("debug",      "aiml_log_debug")
        REG_LOG("info",       "aiml_log_info")
        REG_LOG("warn",       "aiml_log_warn")
        REG_LOG("error",      "aiml_log_error")
        REG_LOG("fatal",      "aiml_log_fatal")
        REG_LOG("deprecated", "aiml_log_deprecated")
        REG_LOG("system",     "aiml_log_system")

#undef REG_LOG

        /* log.set_level(level_str) */
        log.builtinFuncs["set_level"] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            llvm::Function* fn = ctx.module().getFunction("aiml_log_set_level");
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(ctx.builder.getVoidTy(), {p}, false),
                llvm::Function::ExternalLinkage, "aiml_log_set_level", ctx.module());
            llvm::Value* lvl = args.size() > 0 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))
                                                : ctx.builder.CreateGlobalStringPtr("info");
            ctx.builder.CreateCall(fn, {lvl});
            return nullptr;
        };

        /* log.set_file(path_str) */
        log.builtinFuncs["set_file"] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            llvm::Function* fn = ctx.module().getFunction("aiml_log_set_file");
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(ctx.builder.getVoidTy(), {p}, false),
                llvm::Function::ExternalLinkage, "aiml_log_set_file", ctx.module());
            llvm::Value* path = args.size() > 0 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))
                                                 : llvm::Constant::getNullValue(p);
            ctx.builder.CreateCall(fn, {path});
            return nullptr;
        };
    } // end log namespace

    // ═══════════════════════════════════════════════════════════════════════
    // vocab namespace — i18n runtime.  Thin wrapper around
    // src/sys/vocab_runtime.cpp; see spec/44_8_vocab.md for the design.
    // Functions:
    //   vocab.load(langs_json, vocab_json) → i32   0=ok / -1 / -2
    //   vocab.set_language(idx)                    void
    //   vocab.current_language() → i32
    //   vocab.language_count() → i32
    //   vocab.language_code(idx) → string          nullable
    //   vocab.lookup(source) → string              always non-null
    // ═══════════════════════════════════════════════════════════════════════
    {
        auto& vocab = ctx.namespaces["vocab"];
        auto* p   = llvm::PointerType::get(ctx.context, 0);
        auto* vd  = ctx.builder.getVoidTy();
        auto* i32 = ctx.builder.getInt32Ty();

        // vocab.load(langs_json, vocab_json) → i32
        vocab.builtinFuncs["load"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p8   = llvm::PointerType::get(ctx.context, 0);
            auto* i32t = ctx.builder.getInt32Ty();
            llvm::Function* fn = ctx.module().getFunction("aiml_vocab_load");
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(i32t, {p8, p8}, false),
                llvm::Function::ExternalLinkage, "aiml_vocab_load", ctx.module());
            llvm::Value* langs = args.size() > 0
                ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))
                : ctx.builder.CreateGlobalStringPtr("{}");
            llvm::Value* voc = args.size() > 1
                ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
                : ctx.builder.CreateGlobalStringPtr("{}");
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {langs, voc}));
        };

        // vocab.set_language(idx)
        vocab.builtinFuncs["set_language"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* vdt  = ctx.builder.getVoidTy();
            auto* i32t = ctx.builder.getInt32Ty();
            llvm::Function* fn = ctx.module().getFunction("aiml_vocab_set_language");
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(vdt, {i32t}, false),
                llvm::Function::ExternalLinkage, "aiml_vocab_set_language", ctx.module());
            llvm::Value* idx = args.size() > 0
                ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))
                : ctx.builder.getInt32(0);
            if (idx->getType() != i32t) idx = ctx.builder.CreateZExtOrTrunc(idx, i32t);
            ctx.builder.CreateCall(fn, {idx});
            return nullptr;
        };

        // vocab.current_language() → i32
        vocab.builtinFuncs["current_language"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            auto* i32t = ctx.builder.getInt32Ty();
            llvm::Function* fn = ctx.module().getFunction("aiml_vocab_current_language");
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(i32t, {}, false),
                llvm::Function::ExternalLinkage, "aiml_vocab_current_language", ctx.module());
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {}));
        };

        // vocab.language_count() → i32
        vocab.builtinFuncs["language_count"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            auto* i32t = ctx.builder.getInt32Ty();
            llvm::Function* fn = ctx.module().getFunction("aiml_vocab_language_count");
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(i32t, {}, false),
                llvm::Function::ExternalLinkage, "aiml_vocab_language_count", ctx.module());
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {}));
        };

        // vocab.language_code(idx) → string (may be NULL)
        vocab.builtinFuncs["language_code"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p8   = llvm::PointerType::get(ctx.context, 0);
            auto* i32t = ctx.builder.getInt32Ty();
            llvm::Function* fn = ctx.module().getFunction("aiml_vocab_language_code");
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(p8, {i32t}, false),
                llvm::Function::ExternalLinkage, "aiml_vocab_language_code", ctx.module());
            llvm::Value* idx = args.size() > 0
                ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))
                : ctx.builder.getInt32(0);
            if (idx->getType() != i32t) idx = ctx.builder.CreateZExtOrTrunc(idx, i32t);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {idx}));
        };

        // vocab.lookup(source) → string (never NULL — falls back to source)
        vocab.builtinFuncs["lookup"] = [](CompilerContext& ctx,
            const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p8 = llvm::PointerType::get(ctx.context, 0);
            llvm::Function* fn = ctx.module().getFunction("aiml_vocab_lookup");
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(p8, {p8}, false),
                llvm::Function::ExternalLinkage, "aiml_vocab_lookup", ctx.module());
            llvm::Value* src = args.size() > 0
                ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))
                : ctx.builder.CreateGlobalStringPtr("");
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {src}));
        };

        // Suppress "unused variable" warnings on the outer aliases when
        // the closures compile down to nothing referencing them.
        (void)p; (void)vd; (void)i32;
    } // end vocab namespace

    // ═══════════════════════════════════════════════════════════════════════
    // tensor builtin class (instance methods on tensor = i8*)
    // preLoadBase=true → baseAlloca is already the loaded i8* tensor handle
    // ═══════════════════════════════════════════════════════════════════════
    {
        using CI = CompilerContext::ClassInfo;
        CI ci;
        ci.name      = "tensor";
        ci.isBuiltin = true;
        ci.preLoadBase = true;

        // t.free()
        ci.builtinMethods["free"] = [](CompilerContext& ctx, llvm::Value* tPtr,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_tensor_free", ctx.builder.getVoidTy(), {i8p});
            ctx.builder.CreateCall(f, {tPtr});
            return nullptr;
        };

        // t.shape(dim) → i32
        ci.builtinMethods["shape"] = [](CompilerContext& ctx, llvm::Value* tPtr,
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_tensor_shape", i32, {i8p, i32});
            llvm::Value* dim = call->args.empty() ? ctx.builder.getInt32(0)
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {tPtr, dim}));
        };

        // t.ndim() → i32
        ci.builtinMethods["ndim"] = [](CompilerContext& ctx, llvm::Value* tPtr,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_tensor_ndim", ctx.builder.getInt32Ty(), {i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {tPtr}));
        };

        // t.size() → i32
        ci.builtinMethods["size"] = [](CompilerContext& ctx, llvm::Value* tPtr,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_tensor_size", ctx.builder.getInt32Ty(), {i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {tPtr}));
        };

        // t.item() → f32
        ci.builtinMethods["item"] = [](CompilerContext& ctx, llvm::Value* tPtr,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_tensor_item", ctx.builder.getFloatTy(), {i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {tPtr}));
        };

        // t.get(idx) → f32
        ci.builtinMethods["get"] = [](CompilerContext& ctx, llvm::Value* tPtr,
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_tensor_get_flat", ctx.builder.getFloatTy(), {i8p, i32});
            llvm::Value* idx = call->args.empty() ? ctx.builder.getInt32(0)
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {tPtr, idx}));
        };

        // t.set(idx, val)
        ci.builtinMethods["set"] = [](CompilerContext& ctx, llvm::Value* tPtr,
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            auto* f32 = ctx.builder.getFloatTy();
            llvm::Function* f = ctx.getFunction("aiml_tensor_set_flat", ctx.builder.getVoidTy(), {i8p, i32, f32});
            if (call->args.size() < 2) return nullptr;
            llvm::Value* idx = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            llvm::Value* val = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[1].get())));
            ctx.builder.CreateCall(f, {tPtr, idx, val});
            return nullptr;
        };

        // t.fill(val)
        ci.builtinMethods["fill"] = [](CompilerContext& ctx, llvm::Value* tPtr,
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* f32 = ctx.builder.getFloatTy();
            llvm::Function* f = ctx.getFunction("aiml_tensor_fill", ctx.builder.getVoidTy(), {i8p, f32});
            if (call->args.empty()) return nullptr;
            llvm::Value* val = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            ctx.builder.CreateCall(f, {tPtr, val});
            return nullptr;
        };

        // t.grad() → tensor (i8*)
        ci.builtinMethods["grad"] = [](CompilerContext& ctx, llvm::Value* tPtr,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_tensor_grad", i8p, {i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {tPtr}));
        };

        // t.zero_grad()
        ci.builtinMethods["zero_grad"] = [](CompilerContext& ctx, llvm::Value* tPtr,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_tensor_zero_grad", ctx.builder.getVoidTy(), {i8p});
            ctx.builder.CreateCall(f, {tPtr});
            return nullptr;
        };

        // t.keep — spec/50 §14.1: this tensor is the caller's own; begin_graph never collects it.
        ci.builtinMethods["keep"] = [](CompilerContext& ctx, llvm::Value* tPtr,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_tensor_keep", ctx.builder.getVoidTy(), {i8p});
            ctx.builder.CreateCall(f, {tPtr});
            return nullptr;
        };

        // t.add_inplace(src)
        ci.builtinMethods["add_inplace"] = [](CompilerContext& ctx, llvm::Value* tPtr,
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_tensor_add_inplace", ctx.builder.getVoidTy(), {i8p, i8p});
            if (call->args.empty()) return nullptr;
            auto* src = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            ctx.builder.CreateCall(f, {tPtr, src});
            return nullptr;
        };

        // t.scale_inplace(s)
        ci.builtinMethods["scale_inplace"] = [](CompilerContext& ctx, llvm::Value* tPtr,
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* f32 = ctx.builder.getFloatTy();
            llvm::Function* f = ctx.getFunction("aiml_tensor_scale_inplace", ctx.builder.getVoidTy(), {i8p, f32});
            if (call->args.empty()) return nullptr;
            auto* sVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            if (sVal->getType()->isIntegerTy())
                sVal = ctx.builder.CreateSIToFP(sVal, f32);
            ctx.builder.CreateCall(f, {tPtr, sVal});
            return nullptr;
        };

        // t.set_requires_grad(flag)
        ci.builtinMethods["set_requires_grad"] = [](CompilerContext& ctx, llvm::Value* tPtr,
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_tensor_set_requires_grad", ctx.builder.getVoidTy(), {i8p, i32});
            llvm::Value* flag = call->args.empty() ? ctx.builder.getInt32(1)
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            ctx.builder.CreateCall(f, {tPtr, flag});
            return nullptr;
        };

        // t.requires_grad() → i32
        ci.builtinMethods["requires_grad"] = [](CompilerContext& ctx, llvm::Value* tPtr,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_tensor_requires_grad", ctx.builder.getInt32Ty(), {i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {tPtr}));
        };

        // t.print(label)
        ci.builtinMethods["print"] = [](CompilerContext& ctx, llvm::Value* tPtr,
            AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_tensor_print", ctx.builder.getVoidTy(), {i8p, i8p});
            llvm::Value* label = call->args.empty()
                ? ctx.builder.CreateGlobalStringPtr("")
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(evalExpr(call->args[0].get())));
            ctx.builder.CreateCall(f, {tPtr, label});
            return nullptr;
        };

        // t.clone() → tensor
        ci.builtinMethods["clone"] = [](CompilerContext& ctx, llvm::Value* tPtr,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_tensor_clone", i8p, {i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {tPtr}));
        };

        // t.grad_view() → tensor  (shared data, its own grad — no memcpy)
        ci.builtinMethods["grad_view"] = [](CompilerContext& ctx, llvm::Value* tPtr,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_tensor_grad_view", i8p, {i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {tPtr}));
        };

        // t.data_ptr() → ptr (float*)
        ci.builtinMethods["data_ptr"] = [](CompilerContext& ctx, llvm::Value* tPtr,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_tensor_data_ptr", i8p, {i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {tPtr}));
        };

        ctx.classes["tensor"] = std::move(ci);
    }

    // ═══════════════════════════════════════════════════════════════════════
    // tensor namespace (static / factory functions)
    // ═══════════════════════════════════════════════════════════════════════
    {
        auto& tn = ctx.namespaces["tensor"];

        // ── Helper: build a stack [N x i32] from variadic i32 args ────────
        // Used by zeros, ones, randn, reshape — returns {ptr, ndim}.
        // `startIdx` = first arg index that is a shape dimension.
        auto buildShapeArray = [](CompilerContext& ctx, const std::vector<IRValue*>& args, size_t startIdx)
                -> std::pair<llvm::Value*, llvm::Value*> {
            auto* i32 = ctx.builder.getInt32Ty();
            int ndim = (int)args.size() - (int)startIdx;
            if (ndim <= 0) {
                auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                return {llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p)),
                        ctx.builder.getInt32(0)};
            }
            llvm::ArrayType* arrTy = llvm::ArrayType::get(i32, ndim);
            llvm::Value* arrAlloca = ctx.createEntryAlloca(arrTy, "shape_arr");
            for (int i = 0; i < ndim; ++i) {
                llvm::Value* gep = ctx.builder.CreateConstInBoundsGEP2_32(arrTy, arrAlloca, 0, i);
                llvm::Value* val = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[startIdx + i]));
                ctx.builder.CreateStore(val, gep);
            }
            llvm::Value* ptr = ctx.builder.CreateConstInBoundsGEP2_32(arrTy, arrAlloca, 0, 0, "shape_ptr");
            return {ptr, ctx.builder.getInt32(ndim)};
        };

        // tensor.zeros(d0, d1, ...) → tensor
        tn.builtinFuncs["zeros"] = [buildShapeArray](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            auto [shapePtr, ndim] = buildShapeArray(ctx, args, 0);
            llvm::Function* f = ctx.getFunction("aiml_tensor_zeros", i8p, {i8p, i32});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {shapePtr, ndim}));
        };

        // tensor.ones(d0, d1, ...) → tensor
        tn.builtinFuncs["ones"] = [buildShapeArray](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            auto [shapePtr, ndim] = buildShapeArray(ctx, args, 0);
            llvm::Function* f = ctx.getFunction("aiml_tensor_ones", i8p, {i8p, i32});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {shapePtr, ndim}));
        };

        // tensor.randn(d0, d1, ...) → tensor  (mean=0, stddev=1)
        // tensor.randn(mean, stddev, d0, d1, ...) — if first two args are f32
        // For simplicity: always mean=0 stddev=1, all args are shape dims
        tn.builtinFuncs["randn"] = [buildShapeArray](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            auto* f32 = ctx.builder.getFloatTy();
            auto [shapePtr, ndim] = buildShapeArray(ctx, args, 0);
            llvm::Function* f = ctx.getFunction("aiml_tensor_randn", i8p, {i8p, i32, f32, f32});
            llvm::Value* mean   = llvm::ConstantFP::get(f32, 0.0);
            llvm::Value* stddev = llvm::ConstantFP::get(f32, 1.0);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {shapePtr, ndim, mean, stddev}));
        };

        // tensor.seed(n) — spec/50 §15: fix the start of the tensor runtime's generator, so
        // randn/randn_ex give the SAME numbers again, across processes and platforms.
        tn.builtinFuncs["seed"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i64 = ctx.builder.getInt64Ty();
            llvm::Value* seed = llvm::ConstantInt::get(i64, 0);
            if (!args.empty()) {
                llvm::Value* raw = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
                seed = raw->getType()->isIntegerTy() ? ctx.builder.CreateIntCast(raw, i64, true)
                                                     : ctx.builder.CreateFPToSI(raw, i64);
            } else {
                LOG_ERROR(ctx.fmtLoc() + "tensor.seed expects the seed (spec/50 §15)");
            }
            ctx.builder.CreateCall(ctx.getFunction("aiml_tensor_seed", ctx.builder.getVoidTy(), {i64}), {seed});
            return ctx.ir_gen->getInt32(0);
        };

        // tensor.randn_ex(mean, stddev, d0, d1, ...) → tensor
        tn.builtinFuncs["randn_ex"] = [buildShapeArray](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            auto* f32 = ctx.builder.getFloatTy();
            if (args.size() < 3) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            llvm::Value* mean   = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* stddev = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            auto [shapePtr, ndim] = buildShapeArray(ctx, args, 2);
            llvm::Function* f = ctx.getFunction("aiml_tensor_randn", i8p, {i8p, i32, f32, f32});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {shapePtr, ndim, mean, stddev}));
        };

        // tensor.clone(t) → tensor
        tn.builtinFuncs["clone"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            if (args.empty()) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            llvm::Function* f = ctx.getFunction("aiml_tensor_clone", i8p, {i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))}));
        };

        // tensor.free(t)
        tn.builtinFuncs["free"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            if (args.empty()) return nullptr;
            llvm::Function* f = ctx.getFunction("aiml_tensor_free", ctx.builder.getVoidTy(), {i8p});
            ctx.builder.CreateCall(f, {static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))});
            return nullptr;
        };

        // ── Elementwise: (tensor, tensor) → tensor ───────────────────────
        #define REG_TT_TO_T(name, cname) \
        tn.builtinFuncs[name] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* { \
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo(); \
            if (args.size() < 2) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p)); \
            llvm::Function* f = ctx.getFunction(cname, i8p, {i8p, i8p}); \
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, { \
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])), \
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))})); \
        }
        REG_TT_TO_T("add",    "aiml_tensor_add");
        REG_TT_TO_T("sub",    "aiml_tensor_sub");
        REG_TT_TO_T("mul",    "aiml_tensor_mul");
        REG_TT_TO_T("div",    "aiml_tensor_div");
        REG_TT_TO_T("matmul", "aiml_tensor_matmul");
        #undef REG_TT_TO_T

        // ── Unary: (tensor) → tensor ─────────────────────────────────────
        #define REG_T_TO_T(name, cname) \
        tn.builtinFuncs[name] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* { \
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo(); \
            if (args.empty()) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p)); \
            llvm::Function* f = ctx.getFunction(cname, i8p, {i8p}); \
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, { \
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))})); \
        }
        REG_T_TO_T("neg",  "aiml_tensor_neg");
        REG_T_TO_T("exp",  "aiml_tensor_exp");
        REG_T_TO_T("log",  "aiml_tensor_log_op");
        REG_T_TO_T("tanh", "aiml_tensor_tanh_op");
        REG_T_TO_T("relu", "aiml_tensor_relu");
        REG_T_TO_T("gelu", "aiml_tensor_gelu");
        REG_T_TO_T("abs",     "aiml_tensor_abs");
        REG_T_TO_T("sign",    "aiml_tensor_sign");
        REG_T_TO_T("step",    "aiml_tensor_step");
        #undef REG_T_TO_T

        // tensor.fma_inplace(target, src, scale) → void  (no allocation)
        #define REG_FMA_INPLACE(name, cname) \
        tn.builtinFuncs[name] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* { \
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo(); \
            auto* f32 = ctx.builder.getFloatTy(); \
            auto* voidTy = ctx.builder.getVoidTy(); \
            if (args.size() < 3) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p)); \
            llvm::Function* f = ctx.getFunction(cname, voidTy, {i8p, i8p, f32}); \
            ctx.builder.CreateCall(f, { \
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])), \
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])), \
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]))}); \
            return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p)); \
        };
        REG_FMA_INPLACE("fma_inplace",     "aiml_tensor_fma_inplace");
        REG_FMA_INPLACE("fma_sq_inplace",  "aiml_tensor_fma_sq_inplace");
        REG_FMA_INPLACE("fma_abs_inplace", "aiml_tensor_fma_abs_inplace");
        #undef REG_FMA_INPLACE

        // tensor.mul_scalar(t, s) → tensor
        tn.builtinFuncs["mul_scalar"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* f32 = ctx.builder.getFloatTy();
            if (args.size() < 2) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            llvm::Function* f = ctx.getFunction("aiml_tensor_mul_scalar", i8p, {i8p, f32});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))}));
        };

        // ── Reduction: (tensor, dim) → tensor ────────────────────────────
        #define REG_REDUCE(name, cname) \
        tn.builtinFuncs[name] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* { \
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo(); \
            auto* i32 = ctx.builder.getInt32Ty(); \
            if (args.empty()) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p)); \
            llvm::Function* f = ctx.getFunction(cname, i8p, {i8p, i32}); \
            llvm::Value* dim = args.size() >= 2 \
                ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])) \
                : ctx.builder.getInt32(-1); \
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, { \
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])), dim})); \
        }
        REG_REDUCE("sum",  "aiml_tensor_sum");
        REG_REDUCE("mean", "aiml_tensor_mean");
        #undef REG_REDUCE

        // tensor.softmax(t, dim) → tensor
        tn.builtinFuncs["softmax"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            if (args.empty()) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            llvm::Function* f = ctx.getFunction("aiml_tensor_softmax", i8p, {i8p, i32});
            llvm::Value* dim = args.size() >= 2
                ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
                : ctx.builder.getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])), dim}));
        };

        // tensor.layer_norm(t, gamma, beta, eps) → tensor
        tn.builtinFuncs["layer_norm"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* f32 = ctx.builder.getFloatTy();
            if (args.size() < 3) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            llvm::Function* f = ctx.getFunction("aiml_tensor_layer_norm", i8p, {i8p, i8p, i8p, f32});
            llvm::Value* eps = args.size() >= 4
                ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[3]))
                : llvm::ConstantFP::get(f32, 1e-5);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2])),
                eps}));
        };

        // tensor.reshape(t, d0, d1, ...) → tensor
        tn.builtinFuncs["reshape"] = [buildShapeArray](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            if (args.empty()) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            llvm::Value* tPtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            auto [shapePtr, ndim] = buildShapeArray(ctx, args, 1);
            llvm::Function* f = ctx.getFunction("aiml_tensor_reshape", i8p, {i8p, i8p, i32});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {tPtr, shapePtr, ndim}));
        };

        // tensor.transpose(t, d0, d1) → tensor
        tn.builtinFuncs["transpose"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            if (args.size() < 3) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            llvm::Function* f = ctx.getFunction("aiml_tensor_transpose", i8p, {i8p, i32, i32});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]))}));
        };

        // tensor.slice(t, dim, start, end) → tensor
        tn.builtinFuncs["slice"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            if (args.size() < 4) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            llvm::Function* f = ctx.getFunction("aiml_tensor_slice", i8p, {i8p, i32, i32, i32});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[3]))}));
        };

        // tensor.argmax_scalar(t) → i32
        tn.builtinFuncs["argmax_scalar"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            if (args.empty()) return ctx.ir_gen->getInt32(0);
            llvm::Function* f = ctx.getFunction("aiml_tensor_argmax_scalar", i32, {i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))}));
        };

        // tensor.argmax(t, dim) → tensor
        tn.builtinFuncs["argmax"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            if (args.empty()) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            llvm::Function* f = ctx.getFunction("aiml_tensor_argmax", i8p, {i8p, i32});
            llvm::Value* dim = args.size() >= 2
                ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
                : ctx.builder.getInt32(-1);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])), dim}));
        };

        // tensor.embedding_lookup(W, id_tensor, seq_len) → tensor
        // id_tensor = 1D float tensor with float-encoded token IDs
        tn.builtinFuncs["embedding_lookup"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            if (args.size() < 3) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            llvm::Function* f = ctx.getFunction("aiml_tensor_embedding_lookup", i8p, {i8p, i8p, i32});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]))}));
        };

        // tensor.cross_entropy(logits, target_tensor, batch_size) → tensor
        tn.builtinFuncs["cross_entropy"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            if (args.size() < 3) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            llvm::Function* f = ctx.getFunction("aiml_tensor_cross_entropy", i8p, {i8p, i8p, i32});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]))}));
        };

        // tensor.weighted_cross_entropy(logits, targets, weights, seq_len) → tensor
        tn.builtinFuncs["weighted_cross_entropy"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            if (args.size() < 4) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            llvm::Function* f = ctx.getFunction("aiml_tensor_weighted_cross_entropy", i8p, {i8p, i8p, i8p, i32});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[3]))}));
        };

        // tensor.from_data(data_ptr, d0, d1, ...) → tensor
        // data_ptr = i8* (pointer to float array)
        tn.builtinFuncs["from_data"] = [buildShapeArray](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            if (args.empty()) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            llvm::Value* dataPtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            auto [shapePtr, ndim] = buildShapeArray(ctx, args, 1);
            llvm::Function* f = ctx.getFunction("aiml_tensor_from_data", i8p, {i8p, i8p, i32});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {dataPtr, shapePtr, ndim}));
        };

        // tensor.from_int_list(list<i32>, count) → tensor
        // Bulk-converts int list to float tensor (replaces element-by-element loop)
        tn.builtinFuncs["from_int_list"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            auto* i32p = i32->getPointerTo();
            if (args.size() < 2) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            // args[0] = list<i32> struct VALUE (loaded), args[1] = count
            llvm::Value* listVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* count = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            // list struct layout: {i32 size, i32 cap, i32* data}
            // ExtractValue index 2 = data pointer (i32*)
            auto* dataPtr = ctx.builder.CreateExtractValue(listVal, {2});
            llvm::Function* f = ctx.getFunction("aiml_tensor_from_int_list", i8p, {i32p, i32});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {dataPtr, count}));
        };

        // tensor.from_int_ptr(i8* data_ptr, count) → tensor
        // Like from_int_list but takes a raw pointer (e.g. array<i32:N>.data_ptr)
        tn.builtinFuncs["from_int_ptr"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            auto* i32p = i32->getPointerTo();
            if (args.size() < 2) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            llvm::Value* rawPtr = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* count  = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            llvm::Value* dataPtr = ctx.builder.CreateBitCast(rawPtr, i32p);
            llvm::Function* f = ctx.getFunction("aiml_tensor_from_int_list", i8p, {i32p, i32});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {dataPtr, count}));
        };

        // tensor.from_int_list_batch(list<i32>, batch, seq) → tensor [B,S]
        tn.builtinFuncs["from_int_list_batch"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            auto* i32p = i32->getPointerTo();
            if (args.size() < 3) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            llvm::Value* listVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* batch   = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            llvm::Value* seq     = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]));
            auto* dataPtr = ctx.builder.CreateExtractValue(listVal, {2});
            llvm::Function* f = ctx.getFunction("aiml_tensor_from_int_list_batch", i8p, {i32p, i32, i32});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {dataPtr, batch, seq}));
        };

        // ── Arena ────────────────────────────────────────────────────────
        tn.builtinFuncs["arena_init"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            if (args.empty()) return nullptr;
            auto* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_tensor_arena_init", ctx.builder.getVoidTy(), {i32});
            ctx.builder.CreateCall(f, {static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))});
            return nullptr;
        };

        tn.builtinFuncs["arena_free"] = [](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
            llvm::Function* f = ctx.getFunction("aiml_tensor_arena_free", ctx.builder.getVoidTy(), {});
            ctx.builder.CreateCall(f, {});
            return nullptr;
        };

        // tensor.kv_evict(store_name) — free tensor store from RAM, keep WAL
        tn.builtinFuncs["kv_evict"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            if (args.empty()) return nullptr;
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_tensor_kv_evict", ctx.builder.getVoidTy(), {i8p});
            ctx.builder.CreateCall(f, {static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))});
            return nullptr;
        };

        // ── Autograd control ─────────────────────────────────────────────
        tn.builtinFuncs["begin_graph"] = [](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
            llvm::Function* f = ctx.getFunction("aiml_tensor_begin_graph", ctx.builder.getVoidTy(), {});
            ctx.builder.CreateCall(f, {});
            return nullptr;
        };

        tn.builtinFuncs["backward"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            if (args.empty()) return nullptr;
            llvm::Function* f = ctx.getFunction("aiml_tensor_backward", ctx.builder.getVoidTy(), {i8p});
            ctx.builder.CreateCall(f, {static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))});
            return nullptr;
        };

        tn.builtinFuncs["thread_cleanup"] = [](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
            llvm::Function* f = ctx.getFunction("aiml_tensor_thread_cleanup", ctx.builder.getVoidTy(), {});
            ctx.builder.CreateCall(f, {});
            return nullptr;
        };

        tn.builtinFuncs["no_grad_begin"] = [](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
            llvm::Function* f = ctx.getFunction("aiml_tensor_no_grad_begin", ctx.builder.getVoidTy(), {});
            ctx.builder.CreateCall(f, {});
            return nullptr;
        };

        tn.builtinFuncs["no_grad_end"] = [](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
            llvm::Function* f = ctx.getFunction("aiml_tensor_no_grad_end", ctx.builder.getVoidTy(), {});
            ctx.builder.CreateCall(f, {});
            return nullptr;
        };

        // ── Adam optimizer ───────────────────────────────────────────────
        // tensor.adam_step(param, m, v, lr, beta1, beta2, eps, step)
        tn.builtinFuncs["adam_step"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* f32 = ctx.builder.getFloatTy();
            auto* i32 = ctx.builder.getInt32Ty();
            if (args.size() < 8) return nullptr;
            llvm::Function* f = ctx.getFunction("aiml_tensor_adam_step",
                ctx.builder.getVoidTy(), {i8p, i8p, i8p, f32, f32, f32, f32, i32});
            ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[3])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[4])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[5])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[6])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[7]))
            });
            return nullptr;
        };

        // tensor.adam_step_with_grad(param, m, v, grad, lr, beta1, beta2, eps, step)
        // PS variant: grad tensor is passed explicitly (not read from param->grad)
        tn.builtinFuncs["adam_step_with_grad"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* f32 = ctx.builder.getFloatTy();
            auto* i32 = ctx.builder.getInt32Ty();
            if (args.size() < 9) return nullptr;
            llvm::Function* f = ctx.getFunction("aiml_tensor_adam_step_with_grad",
                ctx.builder.getVoidTy(), {i8p, i8p, i8p, i8p, f32, f32, f32, f32, i32});
            ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[3])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[4])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[5])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[6])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[7])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[8]))
            });
            return nullptr;
        };

        // tensor.print(t, label)
        tn.builtinFuncs["print"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            if (args.empty()) return nullptr;
            llvm::Function* f = ctx.getFunction("aiml_tensor_print", ctx.builder.getVoidTy(), {i8p, i8p});
            llvm::Value* label = args.size() >= 2
                ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
                : ctx.builder.CreateGlobalStringPtr("");
            ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])), label});
            return nullptr;
        };

        // tensor.fill(t, val)
        tn.builtinFuncs["fill"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* f32 = ctx.builder.getFloatTy();
            if (args.size() < 2) return nullptr;
            llvm::Function* f = ctx.getFunction("aiml_tensor_fill", ctx.builder.getVoidTy(), {i8p, f32});
            ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))});
            return nullptr;
        };

        // tensor.sha256(t) → string
        tn.builtinFuncs["sha256"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_tensor_sha256", i8p, {i8p});
            llvm::Value* t = args.empty()
                ? llvm::Constant::getNullValue(i8p)
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {t}));
        };

        // tensor.add_inplace(dst, src)
        tn.builtinFuncs["add_inplace"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            if (args.size() < 2) return nullptr;
            llvm::Function* f = ctx.getFunction("aiml_tensor_add_inplace", ctx.builder.getVoidTy(), {i8p, i8p});
            ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))});
            return nullptr;
        };

        // tensor.scale_inplace(t, s)
        tn.builtinFuncs["scale_inplace"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* f32 = ctx.builder.getFloatTy();
            if (args.size() < 2) return nullptr;
            llvm::Function* f = ctx.getFunction("aiml_tensor_scale_inplace", ctx.builder.getVoidTy(), {i8p, f32});
            auto* sVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            if (sVal->getType()->isIntegerTy())
                sVal = ctx.builder.CreateSIToFP(sVal, f32);
            ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                sVal});
            return nullptr;
        };

        // tensor.l2_norm_sq(t) → f32 — sum of squared elements (for global grad norm clipping)
        tn.builtinFuncs["l2_norm_sq"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* f32 = ctx.builder.getFloatTy();
            if (args.empty()) return nullptr;
            llvm::Function* f = ctx.getFunction("aiml_tensor_l2_norm_sq", f32, {i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))}));
        };

        // tensor.clip_scale(norm_sq, clip_norm) → f32 — computes gradient clipping scale factor
        tn.builtinFuncs["clip_scale"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* f32 = ctx.builder.getFloatTy();
            if (args.size() < 2) return nullptr;
            llvm::Function* f = ctx.getFunction("aiml_compute_clip_scale", f32, {f32, f32});
            auto* a0 = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            auto* a1 = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            if (a0->getType()->isIntegerTy()) a0 = ctx.builder.CreateSIToFP(a0, f32);
            if (a1->getType()->isIntegerTy()) a1 = ctx.builder.CreateSIToFP(a1, f32);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {a0, a1}));
        };

        // tensor.save(tensor, store_name, key) — persist tensor to AionDB Kv blob
        tn.builtinFuncs["save"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            if (args.size() < 3) return nullptr;
            llvm::Function* f = ctx.getFunction("aiml_tensor_save", ctx.builder.getVoidTy(), {i8p, i8p, i8p});
            ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]))});
            return nullptr;
        };

        // tensor.load(store_name, key) → tensor (or null if not found)
        tn.builtinFuncs["load"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            if (args.size() < 2) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            llvm::Function* f = ctx.getFunction("aiml_tensor_load", i8p, {i8p, i8p});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))}));
        };

        // tensor.bind(store_name, key, d0, d1, ...) → tensor (zero-copy DB binding)
        tn.builtinFuncs["bind"] = [buildShapeArray](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            if (args.size() < 3) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            auto store_v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            auto key_v   = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            // Shape args start at index 2
            std::vector<IRValue*> shape_args(args.begin() + 2, args.end());
            auto [shapePtr, ndim] = buildShapeArray(ctx, shape_args, 0);
            llvm::Function* f = ctx.getFunction("aiml_tensor_bind", i8p, {i8p, i8p, i8p, i32});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {store_v, key_v, shapePtr, ndim}));
        };

        // tensor.concat(a, b, dim) → tensor
        tn.builtinFuncs["concat"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            if (args.size() < 2) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            llvm::Function* f = ctx.getFunction("aiml_tensor_concat", i8p, {i8p, i8p, i32});
            llvm::Value* dim = args.size() >= 3
                ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2]))
                : ctx.builder.getInt32(0);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])), dim}));
        };
        // tensor.causal_mask(seq_len) → tensor [seq, seq]
        tn.builtinFuncs["causal_mask"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            if (args.empty()) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            llvm::Function* f = ctx.getFunction("aiml_tensor_causal_mask", i8p, {i32});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))}));
        };
        // tensor.fused_mha(x, wq, wk, wv, wo, n_heads, embed_dim) → tensor
        tn.builtinFuncs["fused_mha"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            auto* i32 = ctx.builder.getInt32Ty();
            if (args.size() < 7) return ctx.ir_gen->wrap(llvm::Constant::getNullValue(i8p));
            llvm::Function* f = ctx.getFunction("aiml_tensor_fused_mha", i8p,
                {i8p, i8p, i8p, i8p, i8p, i32, i32});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[3])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[4])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[5])),
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[6]))}));
        };
    } // end tensor namespace
}
