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

    // ── db namespace ─────────────────────────────────────────────────────────
    // db.open("name")      → Store<T>  (reads ctx.deserializeTargetType for elem type)
    // db.open_kv("name")   → Kv<K:V>   (reads ctx.deserializeTargetType for val type)
    // db.open_vec("n", d)  → VecStore
    // db.begin/commit/rollback/checkpoint → stubs (i32 0)
    {
        auto& db = ctx.namespaces["db"];

        // db.open("name") → Store<T>
        db.builtinFuncs["open"] = [](CompilerContext& ctx, const std::vector<IRValue*>& args) -> IRValue* {
            llvm::Type* i8p = ctx.builder.getInt8Ty()->getPointerTo();
            llvm::Type* i64 = ctx.builder.getInt64Ty();

            // Extract element type name from deserializeTargetType "Store<ElementName>"
            std::string targetType = ctx.deserializeTargetType;
            std::string elemName;
            if (targetType.rfind("Store<", 0) == 0 && targetType.back() == '>') {
                elemName = targetType.substr(6, targetType.size() - 7);
            }

            // Compute elem size: look up class or use DataLayout
            int64_t elemSize = 8; // fallback
            if (!elemName.empty()) {
                if (ctx.classes.count(elemName)) {
                    auto& ci = ctx.classes[elemName];
                    if (ci.type) {
                        llvm::DataLayout dl(&ctx.module);
                        elemSize = static_cast<int64_t>(dl.getTypeAllocSize(ci.type));
                    } else if (ci.packed_size > 0) {
                        elemSize = static_cast<int64_t>(ci.packed_size);
                    }
                }
            }

            llvm::Function* f = ctx.getFunction("aiml_db_store_open", i8p, {i8p, i8p, i64});
            llvm::Value* nameVal = args.empty()
                ? ctx.builder.CreateGlobalStringPtr("")
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* tnVal  = ctx.builder.CreateGlobalStringPtr(
                elemName.empty() ? "?" : elemName);
            llvm::Value* szVal  = ctx.builder.getInt64(elemSize);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, {nameVal, tnVal, szVal}));
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

            llvm::Function* f = ctx.getFunction("aiml_db_kv_open", i8p, {i8p, i64});
            llvm::Value* nameVal = args.empty()
                ? ctx.builder.CreateGlobalStringPtr("")
                : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
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

        // Stubs: begin / commit / rollback / checkpoint → return i32(0)
        auto stubFn = [](CompilerContext& ctx, const std::vector<IRValue*>&) -> IRValue* {
            return ctx.ir_gen->getInt32(0);
        };
        db.builtinFuncs["begin"]      = stubFn;
        db.builtinFuncs["commit"]     = stubFn;
        db.builtinFuncs["rollback"]   = stubFn;
        db.builtinFuncs["checkpoint"] = stubFn;
    }
}
