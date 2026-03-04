#define DEBUG_MODE
#include "builtin_registry.h"
#include "ast_nodes.h"
#include "buffer_impl.h"
#include "map_impl.h"
#include "list_impl.h"
#include "code_impl.h"
#include "ir_generator.h"
#include "path_runtime.h"
#include "../../src/sys/net_runtime.h"
#include "../../src/sys/db_runtime.h"
#include "../../src/sys/http_runtime.h"
#include <llvm/IR/IRBuilder.h>

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
static void emitThrow(CompilerContext& ctx) {
    llvm::PointerType* ptrTy = llvm::PointerType::getUnqual(ctx.context);

    llvm::Function* allocFn = ctx.module.getFunction("__cxa_allocate_exception");
    if (!allocFn) {
        auto* fty = llvm::FunctionType::get(ptrTy, {ctx.builder.getInt64Ty()}, false);
        allocFn = llvm::Function::Create(fty, llvm::Function::ExternalLinkage,
            "__cxa_allocate_exception", ctx.module);
    }
    llvm::Function* throwFn = ctx.module.getFunction("__cxa_throw");
    if (!throwFn) {
        auto* fty = llvm::FunctionType::get(ctx.builder.getVoidTy(), {ptrTy, ptrTy, ptrTy}, false);
        throwFn = llvm::Function::Create(fty, llvm::Function::ExternalLinkage,
            "__cxa_throw", ctx.module);
    }

    llvm::Value* excAlloc = ctx.builder.CreateCall(allocFn, {ctx.builder.getInt64(8)}, "exc_alloc");
    ctx.builder.CreateStore(llvm::ConstantPointerNull::get(ptrTy), excAlloc);
    llvm::Value* null = llvm::ConstantPointerNull::get(ptrTy);

    if (ctx.catchBlock) {
        llvm::Function* fn = ctx.builder.GetInsertBlock()->getParent();
        llvm::BasicBlock* unreachBB = llvm::BasicBlock::Create(ctx.context, "throw_unreach", fn);
        ctx.builder.CreateInvoke(throwFn, unreachBB, ctx.catchBlock, {excAlloc, null, null});
        ctx.builder.SetInsertPoint(unreachBB);
    } else {
        ctx.builder.CreateCall(throwFn, {excAlloc, null, null});
    }
    ctx.builder.CreateUnreachable();
}

// Emit a conditional check: if `flag` (i8) != 0, throw; else continue in `continueBB`.
// Returns the new insert-point BB (continueBB) with builder already set.
static llvm::BasicBlock* emitGuard(CompilerContext& ctx, llvm::Value* flag,
                                    const std::string& throwLabel,
                                    const std::string& continueLabel) {
    llvm::Function* fn = ctx.builder.GetInsertBlock()->getParent();
    llvm::Value* isSet = ctx.builder.CreateICmpNE(flag, ctx.builder.getInt8(0), throwLabel + "_cmp");
    llvm::BasicBlock* throwBB    = llvm::BasicBlock::Create(ctx.context, throwLabel, fn);
    llvm::BasicBlock* continueBB = llvm::BasicBlock::Create(ctx.context, continueLabel, fn);
    ctx.builder.CreateCondBr(isSet, throwBB, continueBB);
    ctx.builder.SetInsertPoint(throwBB);
    emitThrow(ctx);
    ctx.builder.SetInsertPoint(continueBB);
    return continueBB;
}

// ---------------------------------------------------------------------------
// registerBuiltins
// ---------------------------------------------------------------------------
void registerBuiltins(CompilerContext& ctx) {

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
    // sys namespace
    // -----------------------------------------------------------------------
    auto& sys = ctx.namespaces["sys"];

    sys.builtinFuncs["exit"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        llvm::Function* f = ctx.getFunction("exit", ctx.builder.getVoidTy(), {ctx.builder.getInt32Ty()});
        llvm::Value* code = args.empty() ? ctx.builder.getInt32(0)
                                         : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        ctx.builder.CreateCall(f, {code});
        return ctx.ir_gen->getInt32(0);
    };

    sys.builtinFuncs["env"] = [](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
        llvm::Type* strTy = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::StructType* mapTy = llvm::StructType::get(ctx.context, {
            ctx.builder.getInt32Ty(), ctx.builder.getInt32Ty(),
            ctx.builder.getInt8Ty()->getPointerTo(),
            strTy->getPointerTo(), strTy->getPointerTo()
        });
        llvm::DataLayout dl(&ctx.module);
        uint64_t mapSize = dl.getTypeAllocSize(mapTy);
        llvm::Function* callocF = ctx.getFunction("calloc",
            ctx.builder.getInt8Ty()->getPointerTo(),
            {ctx.builder.getInt64Ty(), ctx.builder.getInt64Ty()});
        llvm::Value* raw = ctx.builder.CreateCall(callocF,
            {ctx.builder.getInt64(1), ctx.builder.getInt64(mapSize)});
        llvm::Value* mapPtr = ctx.builder.CreateBitCast(raw, mapTy->getPointerTo(), "sys_env_map");
        MapImpl::initMap(ctx, mapPtr, strTy, strTy, 4);
        return ctx.ir_gen->wrap(mapPtr);
    };

    sys.builtinFuncs["size"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        if (args.empty()) return ctx.ir_gen->getInt32(0);
        llvm::Type* ty = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(args[0]->getType()));
        llvm::DataLayout dl(&ctx.module);
        return ctx.ir_gen->wrap(ctx.builder.getInt64(dl.getTypeAllocSize(ty)));
    };

    sys.builtinFuncs["exe"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        if (args.empty()) return ctx.ir_gen->wrap(llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p)));
        llvm::Value* argvList = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        if (argvList->getType()->isStructTy()) {
            llvm::AllocaInst* tmp = ctx.builder.CreateAlloca(argvList->getType());
            ctx.builder.CreateStore(argvList, tmp);
            argvList = ctx.builder.CreateBitCast(tmp, i8p);
        } else if (argvList->getType()->isPointerTy()) {
            argvList = ctx.builder.CreateBitCast(argvList, i8p);
        } else {
            return ctx.ir_gen->getInt32(0);
        }
        llvm::Value* nul  = llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(i8p));
        llvm::Function* f = ctx.getFunction("sys_exe_runtime", i8p,
            {i8p, i8p, i8p, ctx.builder.getInt1Ty(), ctx.builder.getInt32Ty(), ctx.builder.getInt1Ty()});
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(f,
            {argvList, nul, nul, ctx.builder.getInt1(false), ctx.builder.getInt32(-1), ctx.builder.getInt1(false)}));
    };

    // sys.get_arg uses a special outlined helper — registered separately because
    // it needs the __argc/__argv globals (eagerly evaluated index passed as arg).
    sys.builtinFuncs["get_arg"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
        if (args.empty()) return ctx.ir_gen->createGlobalStringPtr("");
        llvm::Value* indexVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
        llvm::GlobalVariable* argvVar = ctx.module.getGlobalVariable("__argv", true);
        llvm::GlobalVariable* argcVar = ctx.module.getGlobalVariable("__argc", true);
        if (!argvVar || !argcVar) return ctx.ir_gen->createGlobalStringPtr("");

        llvm::Function* impl = ctx.module.getFunction("__aiml_sys_get_arg");
        if (!impl) {
            llvm::PointerType* i8p    = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::PointerType* i8pp   = i8p->getPointerTo();
            llvm::Type* i32           = ctx.builder.getInt32Ty();
            llvm::IRBuilder<>::InsertPoint savedIP = ctx.builder.saveIP();
            llvm::FunctionType* ft = llvm::FunctionType::get(i8p, {i32}, false);
            impl = llvm::Function::Create(ft, llvm::Function::InternalLinkage, "__aiml_sys_get_arg", ctx.module);
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
        llvm::Function* dlopen_fn = ctx.module.getFunction("dlopen");
        if (!dlopen_fn) {
            auto* ft = llvm::FunctionType::get(i8p, {i8p, i32}, false);
            dlopen_fn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "dlopen", ctx.module);
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
        llvm::Function* dlsym_fn = ctx.module.getFunction("dlsym");
        if (!dlsym_fn) {
            auto* ft = llvm::FunctionType::get(i8p, {i8p, i8p}, false);
            dlsym_fn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "dlsym", ctx.module);
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
        llvm::Function* f = ctx.getFunction("fopen", i8p, {i8p, i8p});
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
        llvm::Function* strlenF = ctx.getFunction("strlen", ctx.builder.getInt64Ty(), {i8p});
        llvm::Value* len = ctx.builder.CreateCall(strlenF, {text});
        llvm::Function* fwF = ctx.getFunction("fwrite", ctx.builder.getInt64Ty(),
            {i8p, ctx.builder.getInt64Ty(), ctx.builder.getInt64Ty(), i8p});
        llvm::Value* written = ctx.builder.CreateCall(fwF, {text, ctx.builder.getInt64(1), len, handle});
        ctx.builder.CreateBr(mergeBB);
        ctx.builder.SetInsertPoint(mergeBB);
        llvm::PHINode* phi = ctx.builder.CreatePHI(ctx.builder.getInt64Ty(), 2, "write_res");
        phi->addIncoming(ctx.builder.getInt64(0), invalidBB);
        phi->addIncoming(written, validBB);
        return ctx.ir_gen->wrap(ctx.builder.CreateTrunc(phi, ctx.builder.getInt32Ty()));
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
        if (size->getType()->isIntegerTy(32)) size = ctx.builder.CreateZExt(size, ctx.builder.getInt64Ty());
        llvm::BasicBlock* doReadBB = llvm::BasicBlock::Create(ctx.context, "do_read", parentFunc);
        ctx.builder.CreateCondBr(ctx.builder.CreateICmpSGT(size, ctx.builder.getInt64(0)), doReadBB, invalidBB);
        ctx.builder.SetInsertPoint(doReadBB);
        llvm::Function* mallocF = ctx.getFunction("malloc", i8p, {ctx.builder.getInt64Ty()});
        llvm::Value* buf = ctx.builder.CreateCall(mallocF, {ctx.builder.CreateAdd(size, ctx.builder.getInt64(1))});
        llvm::Function* freadF = ctx.getFunction("fread", ctx.builder.getInt64Ty(),
            {i8p, ctx.builder.getInt64Ty(), ctx.builder.getInt64Ty(), i8p});
        llvm::Value* nread = ctx.builder.CreateCall(freadF, {buf, ctx.builder.getInt64(1), size, handle});
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
            {i8p, ctx.builder.getInt64Ty(), ctx.builder.getInt32Ty()});
        llvm::Function* ftellF = ctx.getFunction("ftell", ctx.builder.getInt64Ty(), {i8p});
        llvm::Function* rewindF = ctx.getFunction("rewind", ctx.builder.getVoidTy(), {i8p});
        ctx.builder.CreateCall(fseekF, {handle, ctx.builder.getInt64(0), ctx.builder.getInt32(2)});
        llvm::Value* size = ctx.builder.CreateCall(ftellF, {handle});
        ctx.builder.CreateCall(rewindF, {handle});
        llvm::BasicBlock* doReadBB = llvm::BasicBlock::Create(ctx.context, "do_read_all", parentFunc);
        ctx.builder.CreateCondBr(ctx.builder.CreateICmpSGT(size, ctx.builder.getInt64(0)), doReadBB, invalidBB);
        ctx.builder.SetInsertPoint(doReadBB);
        llvm::Function* mallocF = ctx.getFunction("malloc", i8p, {ctx.builder.getInt64Ty()});
        llvm::Value* buf = ctx.builder.CreateCall(mallocF, {ctx.builder.CreateAdd(size, ctx.builder.getInt64(1))});
        llvm::Function* freadF = ctx.getFunction("fread", ctx.builder.getInt64Ty(),
            {i8p, ctx.builder.getInt64Ty(), ctx.builder.getInt64Ty(), i8p});
        llvm::Value* nread = ctx.builder.CreateCall(freadF, {buf, ctx.builder.getInt64(1), size, handle});
        ctx.builder.CreateStore(ctx.builder.getInt8(0),
            ctx.builder.CreateInBoundsGEP(ctx.builder.getInt8Ty(), buf, nread));
        ctx.builder.CreateBr(mergeBB);
        ctx.builder.SetInsertPoint(mergeBB);
        llvm::PHINode* phi = ctx.builder.CreatePHI(i8p, 2, "read_all_res");
        phi->addIncoming(emptyStr, invalidBB);
        phi->addIncoming(buf, doReadBB);
        return ctx.ir_gen->wrap(phi);
    };

    // -----------------------------------------------------------------------
    // time namespace
    // -----------------------------------------------------------------------
    auto& time = ctx.namespaces["time"];

    time.builtinFuncs["now"] = [](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
        llvm::Function* f = ctx.getFunction("time_now_ms", ctx.builder.getInt64Ty(), {});
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(f));
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
        if (sizeVal) BufferImpl::alloc(ctx, baseAlloca, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(sizeVal)));
        return ctx.ir_gen->getInt32(0);
    };

    bufInfo.builtinMethods["write"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr* call, std::function<IRValue*(AIML::AST::Expr*)> evalExpr) -> IRValue* {
        if (call->args.size() < 2) return ctx.ir_gen->getInt32(0);
        IRValue* val = evalExpr(call->args[0].get());
        std::string typeName = extractTypeName(call->args[1].get());
        llvm::Type* ty = ctx.getLLVMType(typeName);
        if (ty && val) BufferImpl::write(ctx, baseAlloca, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val)), ty);
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

    ctx.classes["buffer"] = std::move(bufInfo);

    // -----------------------------------------------------------------------
    // process builtin class
    // -----------------------------------------------------------------------
    CompilerContext::ClassInfo procInfo;
    procInfo.name      = "process";
    procInfo.isBuiltin = true;

    procInfo.builtinMethods["wait"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Value* procPtr = ctx.builder.CreateLoad(i8p, baseAlloca);
        llvm::Function* f = ctx.getFunction("process_wait", ctx.builder.getInt32Ty(), {i8p});
        return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {procPtr}));
    };

    procInfo.builtinMethods["kill"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
        llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
        llvm::Value* procPtr = ctx.builder.CreateLoad(i8p, baseAlloca);
        llvm::Function* f = ctx.getFunction("process_kill", ctx.builder.getVoidTy(), {i8p});
        ctx.builder.CreateCall(f, {procPtr});
        return ctx.ir_gen->getInt32(0);
    };

    ctx.classes["process"] = std::move(procInfo);

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
        emitGuard(ctx, detachedVal, "join_detached_throw", "join_check_joined");

        // Guard: throw InvalidThreadStateException if already joined
        llvm::Value* joinedGEP = ctx.builder.CreateStructGEP(handleTy, handlePtr, 1, "joined_ptr");
        llvm::Value* joinedVal = ctx.builder.CreateLoad(ctx.builder.getInt8Ty(), joinedGEP, "joined");
        emitGuard(ctx, joinedVal, "join_already_throw", "join_do");

        // pthread_join(tid, NULL)
        llvm::Value* tidGEP = ctx.builder.CreateStructGEP(handleTy, handlePtr, 0, "tid_ptr");
        llvm::Value* tid    = ctx.builder.CreateLoad(ctx.builder.getInt64Ty(), tidGEP, "tid");
        llvm::Type* i8pp = ctx.builder.getInt8Ty()->getPointerTo()->getPointerTo();
        llvm::Function* f = ctx.getFunction("pthread_join",
            ctx.builder.getInt32Ty(), {ctx.builder.getInt64Ty(), i8pp});
        ctx.builder.CreateCall(f, {tid,
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
        emitGuard(ctx, joinedVal, "detach_joined_throw", "detach_check_detached");

        // Guard: throw InvalidThreadStateException if already detached
        llvm::Value* detachedGEP = ctx.builder.CreateStructGEP(handleTy, handlePtr, 2, "detached_ptr");
        llvm::Value* detachedVal = ctx.builder.CreateLoad(ctx.builder.getInt8Ty(), detachedGEP, "detached");
        emitGuard(ctx, detachedVal, "detach_already_throw", "detach_do");

        // pthread_detach(tid)
        llvm::Value* tidGEP = ctx.builder.CreateStructGEP(handleTy, handlePtr, 0, "tid_ptr");
        llvm::Value* tid    = ctx.builder.CreateLoad(ctx.builder.getInt64Ty(), tidGEP, "tid");
        llvm::Function* f = ctx.getFunction("pthread_detach",
            ctx.builder.getInt32Ty(), {ctx.builder.getInt64Ty()});
        ctx.builder.CreateCall(f, {tid});
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
        llvm::Function* mallocFn = ctx.getFunction("malloc", i8p, {ctx.builder.getInt64Ty()});
        llvm::Value* mutexMem = ctx.builder.CreateCall(mallocFn,
            {ctx.builder.getInt64(40)}, "mutex_mem");
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
        pi.builtinMethods["name"] = [callP2P](CompilerContext& ctx, llvm::Value* alloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            return callP2P(ctx, alloca, "aiml_path_name");
        };
        pi.builtinMethods["stem"] = [callP2P](CompilerContext& ctx, llvm::Value* alloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            return callP2P(ctx, alloca, "aiml_path_stem");
        };
        pi.builtinMethods["extension"] = [callP2P](CompilerContext& ctx, llvm::Value* alloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            return callP2P(ctx, alloca, "aiml_path_extension");
        };
        pi.builtinMethods["root"] = [callP2P](CompilerContext& ctx, llvm::Value* alloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            return callP2P(ctx, alloca, "aiml_path_root");
        };
        pi.builtinMethods["to_string"] = [callP2P](CompilerContext& ctx, llvm::Value* alloca,
            AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
            return callP2P(ctx, alloca, "aiml_path_to_string");
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

        io.builtinFuncs["read_file"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Function* f = ctx.getFunction("aiml_io_read_file", i8p, {i8p});
            if (args.empty()) return ctx.ir_gen->wrap(ctx.builder.CreateGlobalStringPtr(""));
            llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {v}));
        };

        // io.write_file(path, content) -> i32  (0=ok, -1=error)
        io.builtinFuncs["write_file"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p  = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32  = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_io_write_file", i32, {i8p, i8p});
            if (args.size() < 2) return ctx.ir_gen->getInt32(-1);
            llvm::Value* p = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* c = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {p, c}));
        };

        // io.list_dir(path) -> list<path>
        // Allocates a GenericList on the stack, inits it, calls aiml_io_list_dir_fill, returns list ptr.
        io.builtinFuncs["list_dir"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p   = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32ty = ctx.builder.getInt32Ty();
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);

            llvm::StructType* listTy = ListImpl::getListStructType(ctx);
            llvm::Value* listAlloca  = ctx.builder.CreateAlloca(listTy, nullptr, "listdir_alloca");
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
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
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
                static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
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

        ctx.classes["ws_handle"] = std::move(wsInfo);
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
        auto& db = ctx.namespaces["db"];

        // db.open("name") → Store<T>
        // Options pre-parsed into ctx.dbOpenPersist / ctx.dbOpenColumnLayout by visitCall.
        db.builtinFuncs["open"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p   = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32   = ctx.builder.getInt32Ty();
            llvm::Type* i64   = ctx.builder.getInt64Ty();
            llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);

            std::string targetType = ctx.deserializeTargetType;
            std::string elemName;
            if (targetType.rfind("Store<", 0) == 0 && targetType.back() == '>')
                elemName = targetType.substr(6, targetType.size() - 7);

            int64_t elemSize = 8;
            if (!elemName.empty() && ctx.classes.count(elemName)) {
                auto& ci = ctx.classes[elemName];
                if (ci.type) {
                    llvm::DataLayout dl(&ctx.module);
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
                llvm::DataLayout dl(&ctx.module);
                int n = (int)ci.members.size();
                std::vector<std::pair<int,int>> flds;
                for (auto& [fname, fm] : ci.members)
                    if (fm.type) flds.push_back({fm.index, (int)dl.getTypeAllocSize(fm.type)});
                std::sort(flds.begin(), flds.end());

                auto* arrTy = llvm::ArrayType::get(i32, (size_t)n);
                std::vector<llvm::Constant*> cs;
                for (auto& [idx, sz] : flds) cs.push_back(ctx.builder.getInt32(sz));
                auto* gv = new llvm::GlobalVariable(ctx.module, arrTy, true,
                    llvm::GlobalValue::InternalLinkage,
                    llvm::ConstantArray::get(arrTy, cs), ".col_fsizes");

                llvm::Function* f = ctx.getFunction("aiml_db_store_open_col",
                    i8p, {i8p, i8p, i32, ptrTy, i64});
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(f,
                    {nameVal, tnVal, ctx.builder.getInt32(n), gv,
                     ctx.builder.getInt64(elemSize)}));
            }

            // ── Persistent (WAL + mmap) ───────────────────────────────────────
            if (ctx.dbOpenPersist) {
                llvm::Value* pathVal = ctx.dbOpenBasePathVal
                    ? ctx.dbOpenBasePathVal
                    : ctx.builder.CreateGlobalStringPtr("./aiondb/");
                llvm::Function* f = ctx.getFunction("aiml_db_store_open_persist",
                    i8p, {i8p, i8p, i64, i8p});
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(f,
                    {nameVal, tnVal, ctx.builder.getInt64(elemSize), pathVal}));
            }

            // ── Default: in-memory ────────────────────────────────────────────
            llvm::Function* f = ctx.getFunction("aiml_db_store_open", i8p, {i8p, i8p, i64});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f,
                {nameVal, tnVal, ctx.builder.getInt64(elemSize)}));
        };

        // db.open_kv("name") → Kv<K:V>
        db.builtinFuncs["open_kv"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i64 = ctx.builder.getInt64Ty();

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
                llvm::DataLayout dl(&ctx.module);
                valSize = static_cast<int64_t>(dl.getTypeAllocSize(ctx.classes[vName].type));
            }

            llvm::Value* nameVal = args.empty()
                ? ctx.builder.CreateGlobalStringPtr("")
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            if (ctx.dbOpenPersist) {
                llvm::Value* pathVal = ctx.dbOpenBasePathVal
                    ? ctx.dbOpenBasePathVal
                    : ctx.builder.CreateGlobalStringPtr("./aiondb/");
                llvm::Function* f = ctx.getFunction("aiml_db_kv_open_persist", i8p, {i8p, i64, i8p});
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(f,
                    {nameVal, ctx.builder.getInt64(valSize), pathVal}));
            }
            llvm::Function* f = ctx.getFunction("aiml_db_kv_open", i8p, {i8p, i64});
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {nameVal, ctx.builder.getInt64(valSize)}));
        };

        // db.open_vec("name", dims) → VecStore
        db.builtinFuncs["open_vec"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i32 = ctx.builder.getInt32Ty();
            llvm::Function* f = ctx.getFunction("aiml_db_vec_open", i8p, {i8p, i32});
            llvm::Value* nameVal = args.empty()
                ? ctx.builder.CreateGlobalStringPtr("")
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* dimsVal = args.size() >= 2
                ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))
                : ctx.builder.getInt32(0);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {nameVal, dimsVal}));
        };

        // db.open_timeseries("name") → TimeSeries<T>
        db.builtinFuncs["open_timeseries"] = [](CompilerContext& ctx,
                const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i64 = ctx.builder.getInt64Ty();

            std::string targetType = ctx.deserializeTargetType;
            std::string elemName;
            if (targetType.rfind("TimeSeries<", 0) == 0 && targetType.back() == '>')
                elemName = targetType.substr(11, targetType.size() - 12);

            int64_t elemSize = 8;
            if (!elemName.empty() && ctx.classes.count(elemName)) {
                auto& ci = ctx.classes[elemName];
                if (ci.type) {
                    llvm::DataLayout dl(&ctx.module);
                    elemSize = static_cast<int64_t>(dl.getTypeAllocSize(ci.type));
                } else if (ci.packed_size > 0) {
                    elemSize = static_cast<int64_t>(ci.packed_size);
                }
            }

            llvm::Value* nameVal = args.empty()
                ? ctx.builder.CreateGlobalStringPtr("")
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            if (ctx.dbOpenPersist) {
                llvm::Value* pathVal = ctx.dbOpenBasePathVal
                    ? ctx.dbOpenBasePathVal
                    : ctx.builder.CreateGlobalStringPtr("./aiondb/");
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

            // Predicate function pointer (NULL if no where clause)
            llvm::Value* predPtr = llvm::ConstantPointerNull::get(
                llvm::cast<llvm::PointerType>(i8p));
            if (!ctx.dbQueryWhere.empty()) {
                llvm::Function* predFn = ctx.module.getFunction(ctx.dbQueryWhere);
                if (predFn) {
                    predPtr = ctx.builder.CreateBitCast(predFn, i8p, "pred_ptr");
                } else {
                    LOG_ERROR("db.query: predicate function '" + ctx.dbQueryWhere + "' not found");
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
                        llvm::DataLayout dl(&ctx.module);
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

            // Allocate result list
            llvm::StructType* listTy = ListImpl::getListStructType(ctx);
            llvm::Value* listAlloca  = ctx.builder.CreateAlloca(listTy, nullptr, "query_list");
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
            llvm::Function* fn = ctx.module.getFunction(fnName);
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(i8p, {i8p}, false),
                llvm::Function::ExternalLinkage, fnName, ctx.module);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base}));
        };
        auto callGetI32 = [&](const char* fnName, llvm::Value* base) -> IRValue* {
            llvm::Function* fn = ctx.module.getFunction(fnName);
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(i32t, {i8p}, false),
                llvm::Function::ExternalLinkage, fnName, ctx.module);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base}));
        };
        auto callGetI8 = [&](const char* fnName, llvm::Value* base) -> IRValue* {
            llvm::Function* fn = ctx.module.getFunction(fnName);
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(i8t, {i8p}, false),
                llvm::Function::ExternalLinkage, fnName, ctx.module);
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
#define URL_GETTER_STR(mName, cFn) \
            ci.builtinMethods[mName] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* { \
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo(); \
                llvm::Value* ptr = ctx.builder.CreateLoad(i8p, base, "url_ptr"); \
                llvm::Function* fn = ctx.module.getFunction(cFn); \
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {i8p}, false), llvm::Function::ExternalLinkage, cFn, ctx.module); \
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {ptr})); \
            }
            URL_GETTER_STR("protocol",  "aiml_url_protocol");
            URL_GETTER_STR("host",      "aiml_url_host");
            URL_GETTER_STR("path",      "aiml_url_path");
            URL_GETTER_STR("fragment",  "aiml_url_fragment");
            URL_GETTER_STR("get_param", "aiml_url_get_param");
            URL_GETTER_STR("to_string", "aiml_url_to_string");
#undef URL_GETTER_STR
            ci.builtinMethods["port"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Value* ptr = ctx.builder.CreateLoad(i8p, base, "url_ptr");
                llvm::Function* fn = ctx.module.getFunction("aiml_url_port");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt32Ty(), {i8p}, false), llvm::Function::ExternalLinkage, "aiml_url_port", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {ptr}));
            };
            ci.builtinMethods["is_https"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Value* ptr = ctx.builder.CreateLoad(i8p, base, "url_ptr");
                llvm::Function* fn = ctx.module.getFunction("aiml_url_is_https");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt8Ty(), {i8p}, false), llvm::Function::ExternalLinkage, "aiml_url_is_https", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {ptr}));
            };
            ctx.classes["url"] = std::move(ci);
        }

        // ── http namespace functions ──────────────────────────────────────
        {
            auto& http = ctx.namespaces["http"];
            // http.get(url) → http_response_handle
            http.builtinFuncs["get"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                if (args.empty()) return ctx.ir_gen->getInt32(0);
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_http_get");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_get", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))}));
            };
            // http.post(url, body) → http_response_handle
            http.builtinFuncs["post"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                if (args.size() < 2) return ctx.ir_gen->getInt32(0);
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_http_post");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_post", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])), static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))}));
            };
            // http.put(url, body) → http_response_handle
            http.builtinFuncs["put"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                if (args.size() < 2) return ctx.ir_gen->getInt32(0);
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_http_put");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_put", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])), static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))}));
            };
            // http.delete(url) → http_response_handle
            http.builtinFuncs["delete"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                if (args.empty()) return ctx.ir_gen->getInt32(0);
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_http_delete");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_delete", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))}));
            };
            // http.head(url) → http_response_handle
            http.builtinFuncs["head"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                if (args.empty()) return ctx.ir_gen->getInt32(0);
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_http_head");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_head", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))}));
            };
            // http.new_request(method, url) → http_client_handle
            http.builtinFuncs["new_request"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                if (args.size() < 2) return ctx.ir_gen->getInt32(0);
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_http_new_request");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_new_request", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])), static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))}));
            };
            // http.parse_url(str) → url
            http.builtinFuncs["parse_url"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                if (args.empty()) return ctx.ir_gen->getInt32(0);
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_url_parse");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {i8p}, false), llvm::Function::ExternalLinkage, "aiml_url_parse", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))}));
            };
            // http.server(host, port) → http_server_handle
            http.builtinFuncs["server"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                if (args.size() < 2) return ctx.ir_gen->getInt32(0);
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_http_server");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {i8p, ctx.builder.getInt32Ty()}, false), llvm::Function::ExternalLinkage, "aiml_http_server", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])), static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))}));
            };
            // http.server_tls(host, port, cert, key) → http_server_handle
            http.builtinFuncs["server_tls"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
                if (args.size() < 4) return ctx.ir_gen->getInt32(0);
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_http_server_tls");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {i8p, ctx.builder.getInt32Ty(), i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_server_tls", ctx.module);
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
                llvm::Function* fn = ctx.module.getFunction("aiml_fs_options_new");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {}, false), llvm::Function::ExternalLinkage, "aiml_fs_options_new", ctx.module);
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
                llvm::Function* fn = ctx.module.getFunction("aiml_http_server_route");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, i8p, i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_server_route", ctx.module);
                if (!call || call->args.size() < 3) return nullptr;
                IRValue* a0 = evalExpr(call->args[0].get());
                IRValue* a1 = evalExpr(call->args[1].get());
                // arg[2] is a function name → get its LLVM Function* and bitcast to i8*
                llvm::Value* handlerPtr = nullptr;
                if (auto* ve = dynamic_cast<AIML::AST::VariableExpr*>(call->args[2].get())) {
                    llvm::Function* hf = ctx.module.getFunction(ve->name);
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
                llvm::Function* fn = ctx.module.getFunction("aiml_http_server_use");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_server_use", ctx.module);
                if (!call || call->args.empty()) return nullptr;
                llvm::Value* mwPtr = nullptr;
                if (auto* ve = dynamic_cast<AIML::AST::VariableExpr*>(call->args[0].get()))
                    if (llvm::Function* mf = ctx.module.getFunction(ve->name))
                        mwPtr = ctx.builder.CreateBitCast(mf, i8p);
                if (!mwPtr) { IRValue* a = evalExpr(call->args[0].get()); mwPtr = a ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a)) : llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p)); }
                ctx.builder.CreateCall(fn, {base, mwPtr});
                return nullptr;
            };
            // srv.group(prefix, middleware?) → http_route_handle
            ci.builtinMethods["group"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_http_server_group");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {i8p, i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_server_group", ctx.module);
                if (!call || call->args.empty()) return ctx.ir_gen->getInt32(0);
                IRValue* prefix = evalExpr(call->args[0].get());
                llvm::Value* mwPtr = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p));
                if (call->args.size() > 1) {
                    if (auto* ve = dynamic_cast<AIML::AST::VariableExpr*>(call->args[1].get()))
                        if (llvm::Function* mf = ctx.module.getFunction(ve->name))
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
                llvm::Function* fn = ctx.module.getFunction("aiml_http_server_set_header");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_server_set_header", ctx.module);
                if (!call || call->args.size() < 2) return nullptr;
                IRValue* a0 = evalExpr(call->args[0].get());
                IRValue* a1 = evalExpr(call->args[1].get());
                ctx.builder.CreateCall(fn, {base, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a0)), static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a1))});
                return nullptr;
            };
            // srv.start() — blocking
            ci.builtinMethods["start"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_http_server_start");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_server_start", ctx.module);
                ctx.builder.CreateCall(fn, {base});
                return nullptr;
            };
            // srv.start_async() → thread_handle
            ci.builtinMethods["start_async"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_http_server_start_async");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_server_start_async", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base}));
            };
            // srv.stop()
            ci.builtinMethods["stop"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_http_server_stop");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_server_stop", ctx.module);
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
                    llvm::Function* fn = ctx.module.getFunction(cFn);
                    if (hasPrefix) {
                        // route(method, path, handler)
                        if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, i8p, i8p, i8p}, false), llvm::Function::ExternalLinkage, cFn, ctx.module);
                        if (!call || call->args.size() < 3) return nullptr;
                        IRValue* a0 = evalExpr(call->args[0].get());
                        IRValue* a1 = evalExpr(call->args[1].get());
                        llvm::Value* hPtr = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p));
                        if (auto* ve = dynamic_cast<AIML::AST::VariableExpr*>(call->args[2].get()))
                            if (llvm::Function* hf = ctx.module.getFunction(ve->name))
                                hPtr = ctx.builder.CreateBitCast(hf, i8p);
                        ctx.builder.CreateCall(fn, {base, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a0)), static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a1)), hPtr});
                    } else {
                        // use(middleware)
                        if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, i8p}, false), llvm::Function::ExternalLinkage, cFn, ctx.module);
                        if (!call || call->args.empty()) return nullptr;
                        llvm::Value* mwPtr = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p));
                        if (auto* ve = dynamic_cast<AIML::AST::VariableExpr*>(call->args[0].get()))
                            if (llvm::Function* mf = ctx.module.getFunction(ve->name))
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
                llvm::Function* fn = ctx.module.getFunction("aiml_http_route_group");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {i8p, i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_route_group", ctx.module);
                if (!call || call->args.empty()) return ctx.ir_gen->getInt32(0);
                IRValue* prefix = evalExpr(call->args[0].get());
                llvm::Value* mwPtr = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p));
                if (call->args.size() > 1) {
                    if (auto* ve = dynamic_cast<AIML::AST::VariableExpr*>(call->args[1].get()))
                        if (llvm::Function* mf = ctx.module.getFunction(ve->name))
                            mwPtr = ctx.builder.CreateBitCast(mf, i8p);
                }
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(prefix)), mwPtr}));
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
                llvm::Function* fn = ctx.module.getFunction("aiml_http_resp_status");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt32Ty(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "aiml_http_resp_status", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base}));
            };
            ci.builtinMethods["ok"]      = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Function* fn = ctx.module.getFunction("aiml_http_resp_ok");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt8Ty(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "aiml_http_resp_ok", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base}));
            };
            ci.builtinMethods["body"]    = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Function* fn = ctx.module.getFunction("aiml_http_resp_body");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "aiml_http_resp_body", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base}));
            };
            // headers field (returns AimlHttpMap* for chained dispatch)
            ci.builtinMethods["headers"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Function* fn = ctx.module.getFunction("aiml_http_resp_get_headers");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "aiml_http_resp_get_headers", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base}));
            };
            // Methods: send, write, write_bytes, flush, close, free
            ci.builtinMethods["send"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Function* fn = ctx.module.getFunction("aiml_http_resp_send");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "aiml_http_resp_send", ctx.module);
                ctx.builder.CreateCall(fn, {base}); return nullptr;
            };
            ci.builtinMethods["write"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_http_resp_write");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_resp_write", ctx.module);
                if (call && !call->args.empty()) {
                    IRValue* a = evalExpr(call->args[0].get());
                    if (a) ctx.builder.CreateCall(fn, {base, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a))});
                }
                return nullptr;
            };
            ci.builtinMethods["flush"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Function* fn = ctx.module.getFunction("aiml_http_resp_flush");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "aiml_http_resp_flush", ctx.module);
                ctx.builder.CreateCall(fn, {base}); return nullptr;
            };
            ci.builtinMethods["close"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Function* fn = ctx.module.getFunction("aiml_http_resp_close");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "aiml_http_resp_close", ctx.module);
                ctx.builder.CreateCall(fn, {base}); return nullptr;
            };
            ci.builtinMethods["free"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Function* fn = ctx.module.getFunction("aiml_http_resp_free");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "aiml_http_resp_free", ctx.module);
                ctx.builder.CreateCall(fn, {base}); return nullptr;
            };
            // Property setters (called via visitSetStmt → builtinSetters)
            ci.builtinSetters["status"] = [](CompilerContext& ctx, llvm::Value* base, AIML::IRValue* newVal) {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_http_resp_set_status");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, ctx.builder.getInt32Ty()}, false), llvm::Function::ExternalLinkage, "aiml_http_resp_set_status", ctx.module);
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(newVal));
                if (v->getType()->isPointerTy()) v = ctx.builder.CreatePtrToInt(v, ctx.builder.getInt32Ty());
                ctx.builder.CreateCall(fn, {base, v});
            };
            ci.builtinSetters["body"] = [](CompilerContext& ctx, llvm::Value* base, AIML::IRValue* newVal) {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_http_resp_set_body");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_resp_set_body", ctx.module);
                ctx.builder.CreateCall(fn, {base, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(newVal))});
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
                llvm::Function* fn = ctx.module.getFunction("aiml_http_req_method");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "aiml_http_req_method", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base}));
            };
            ci.builtinMethods["path"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Function* fn = ctx.module.getFunction("aiml_http_req_path");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "aiml_http_req_path", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base}));
            };
            ci.builtinMethods["body"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Function* fn = ctx.module.getFunction("aiml_http_req_body");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "aiml_http_req_body", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base}));
            };
            ci.builtinMethods["url"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Function* fn = ctx.module.getFunction("aiml_http_req_url");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "aiml_http_req_url", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base}));
            };
            // params / cookies — AimlHttpMap* for chained dispatch (req.params.get(k))
            ci.builtinMethods["params"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Function* fn = ctx.module.getFunction("aiml_http_req_params");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "aiml_http_req_params", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base}));
            };
            ci.builtinMethods["cookies"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Function* fn = ctx.module.getFunction("aiml_http_req_cookies");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "aiml_http_req_cookies", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base}));
            };
            // req.header(name) — method with arg
            ci.builtinMethods["header"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_http_req_header");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_req_header", ctx.module);
                llvm::Value* nameArg = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p));
                if (call && !call->args.empty()) { IRValue* a = evalExpr(call->args[0].get()); if (a) nameArg = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a)); }
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base, nameArg}));
            };
            // req.cookie(name) — method with arg
            ci.builtinMethods["cookie"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr* call, auto evalExpr) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_http_req_cookie");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_req_cookie", ctx.module);
                llvm::Value* nameArg = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8p));
                if (call && !call->args.empty()) { IRValue* a = evalExpr(call->args[0].get()); if (a) nameArg = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(a)); }
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base, nameArg}));
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
                llvm::Function* fn = ctx.module.getFunction("aiml_http_client_get_headers");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "aiml_http_client_get_headers", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base}));
            };
            // client.send() → http_response_handle
            ci.builtinMethods["send"] = [](CompilerContext& ctx, llvm::Value* base, AIML::AST::CallExpr*, auto) -> IRValue* {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_http_client_send");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(i8p, {i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_client_send", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {base}));
            };
            // Property setters
            ci.builtinSetters["body"] = [](CompilerContext& ctx, llvm::Value* base, AIML::IRValue* newVal) {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_http_client_set_body");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_http_client_set_body", ctx.module);
                ctx.builder.CreateCall(fn, {base, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(newVal))});
            };
            ci.builtinSetters["timeout"] = [](CompilerContext& ctx, llvm::Value* base, AIML::IRValue* newVal) {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_http_client_set_timeout");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, ctx.builder.getInt32Ty()}, false), llvm::Function::ExternalLinkage, "aiml_http_client_set_timeout", ctx.module);
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(newVal));
                if (v->getType()->isPointerTy()) v = ctx.builder.CreatePtrToInt(v, ctx.builder.getInt32Ty());
                ctx.builder.CreateCall(fn, {base, v});
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
                llvm::Function* fn = ctx.module.getFunction("aiml_fs_options_set_index");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, i8p}, false), llvm::Function::ExternalLinkage, "aiml_fs_options_set_index", ctx.module);
                ctx.builder.CreateCall(fn, {base, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(newVal))});
            };
            ci.builtinSetters["spa"] = [](CompilerContext& ctx, llvm::Value* base, AIML::IRValue* newVal) {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_fs_options_set_spa");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, ctx.builder.getInt8Ty()}, false), llvm::Function::ExternalLinkage, "aiml_fs_options_set_spa", ctx.module);
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(newVal));
                if (!v->getType()->isIntegerTy(8)) v = ctx.builder.CreateTrunc(v, ctx.builder.getInt8Ty());
                ctx.builder.CreateCall(fn, {base, v});
            };
            ci.builtinSetters["cache_max_age"] = [](CompilerContext& ctx, llvm::Value* base, AIML::IRValue* newVal) {
                llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::Function* fn = ctx.module.getFunction("aiml_fs_options_set_cache_max_age");
                if (!fn) fn = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {i8p, ctx.builder.getInt32Ty()}, false), llvm::Function::ExternalLinkage, "aiml_fs_options_set_cache_max_age", ctx.module);
                llvm::Value* v = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(newVal));
                if (v->getType()->isPointerTy()) v = ctx.builder.CreatePtrToInt(v, ctx.builder.getInt32Ty());
                ctx.builder.CreateCall(fn, {base, v});
            };
            ctx.classes["fs_options"] = std::move(ci);
        }
    }
}
