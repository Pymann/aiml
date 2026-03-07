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
#include "../../src/sys/compress_runtime.h"
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
    // zip_entry — archive entry struct for compress.zip_entries
    // Layout: { ptr name, i64 size, i64 compressed_size, i32 is_dir, i32 crc32, i32 method, i32 _pad }
    // -----------------------------------------------------------------------
    {
        llvm::Type* p   = llvm::PointerType::get(ctx.context, 0);
        llvm::Type* i64 = ctx.builder.getInt64Ty();
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
        ctx.classes["zip_entry"] = std::move(zi);
    }

    // -----------------------------------------------------------------------
    // tar_entry — archive entry struct for compress.tar_entries
    // Layout: { ptr name, i64 size, i32 is_dir, i32 mode }
    // -----------------------------------------------------------------------
    {
        llvm::Type* p   = llvm::PointerType::get(ctx.context, 0);
        llvm::Type* i64 = ctx.builder.getInt64Ty();
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
        ctx.classes["tar_entry"] = std::move(ti);
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

    bufInfo.builtinMethods["len"] = [](CompilerContext& ctx, llvm::Value* baseAlloca,
        AIML::AST::CallExpr*, std::function<IRValue*(AIML::AST::Expr*)>) -> IRValue* {
        auto* sizePtr = ctx.builder.CreateStructGEP(ctx.getLLVMType("buffer"), baseAlloca, 0, "buf_len_ptr");
        return ctx.ir_gen->wrap(ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), sizePtr, "buf_len"));
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
            llvm::AllocaInst* a = ctx.builder.CreateAlloca(ty, nullptr, "mtmp");
            ctx.builder.CreateStore(val, a);
            return a;
        };

        // Get or declare a void C function with given pointer arg types
        auto getVoidFn = [&](const char* name, std::vector<llvm::Type*> args) -> llvm::Function* {
            llvm::Function* fn = ctx.module.getFunction(name);
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(voidT, args, false),
                llvm::Function::ExternalLinkage, name, ctx.module);
            return fn;
        };
        auto getF32Fn = [&](const char* name, std::vector<llvm::Type*> args) -> llvm::Function* {
            llvm::Function* fn = ctx.module.getFunction(name);
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(f32, args, false),
                llvm::Function::ExternalLinkage, name, ctx.module);
            return fn;
        };
        auto getI32Fn = [&](const char* name, std::vector<llvm::Type*> args) -> llvm::Function* {
            llvm::Function* fn = ctx.module.getFunction(name);
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(i32, args, false),
                llvm::Function::ExternalLinkage, name, ctx.module);
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
                llvm::Function* fn = ctx.module.getFunction("aiml_math_mat4_get");
                if (!fn) fn = llvm::Function::Create(
                    llvm::FunctionType::get(ctx.builder.getFloatTy(),
                        {llvm::PointerType::get(ctx.context,0), ctx.builder.getInt32Ty(), ctx.builder.getInt32Ty()}, false),
                    llvm::Function::ExternalLinkage, "aiml_math_mat4_get", ctx.module);
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
                llvm::Function* fn = ctx.module.getFunction("aiml_math_mat4_set");
                if (!fn) fn = llvm::Function::Create(
                    llvm::FunctionType::get(ctx.builder.getVoidTy(),
                        {llvm::PointerType::get(ctx.context,0), ctx.builder.getInt32Ty(), ctx.builder.getInt32Ty(), ctx.builder.getFloatTy()}, false),
                    llvm::Function::ExternalLinkage, "aiml_math_mat4_set", ctx.module);
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
            llvm::Function* fn = ctx.module.getFunction("aiml_math_quat_identity");
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(ctx.builder.getVoidTy(), {llvm::PointerType::get(ctx.context,0)}, false),
                llvm::Function::ExternalLinkage, "aiml_math_quat_identity", ctx.module);
            llvm::AllocaInst* out = ctx.builder.CreateAlloca(quatTy, nullptr, "qi");
            ctx.builder.CreateCall(fn, {out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(quatTy, out, "qi_val"));
        };
        math.builtinFuncs["mat4_identity"] = [mat4Ty](CompilerContext& ctx, const std::vector<AIML::IRValue*>&) -> AIML::IRValue* {
            llvm::Function* fn = ctx.module.getFunction("aiml_math_mat4_identity");
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(ctx.builder.getVoidTy(), {llvm::PointerType::get(ctx.context,0)}, false),
                llvm::Function::ExternalLinkage, "aiml_math_mat4_identity", ctx.module);
            llvm::AllocaInst* out = ctx.builder.CreateAlloca(mat4Ty, nullptr, "mi");
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

        // Declare libm f32 functions once
        auto libmF1 = [&](const char* aimlName, const char* cName) {
            math.builtinFuncs[aimlName] = [cName, toF32](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                llvm::Function* fn = ctx.module.getFunction(cName);
                if (!fn) fn = llvm::Function::Create(
                    llvm::FunctionType::get(ctx.builder.getFloatTy(), {ctx.builder.getFloatTy()}, false),
                    llvm::Function::ExternalLinkage, cName, ctx.module);
                llvm::Value* v = args.empty() ? llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0)
                                             : toF32(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])));
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {v}));
            };
        };
        libmF1("sin",   "sinf");
        libmF1("cos",   "cosf");
        libmF1("tan",   "tanf");
        libmF1("asin",  "asinf");
        libmF1("acos",  "acosf");
        libmF1("sqrt",  "sqrtf");
        libmF1("log",   "logf");
        libmF1("log2",  "log2f");
        libmF1("floor", "floorf");
        libmF1("ceil",  "ceilf");
        libmF1("round", "roundf");
        libmF1("abs",   "fabsf");

        // atan2 (2-arg)
        math.builtinFuncs["atan2"] = [toF32](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            llvm::Function* fn = ctx.module.getFunction("atan2f");
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(ctx.builder.getFloatTy(),
                    {ctx.builder.getFloatTy(), ctx.builder.getFloatTy()}, false),
                llvm::Function::ExternalLinkage, "atan2f", ctx.module);
            auto g = [&](int i) { return args.size()>(size_t)i ? toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]))) : llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0); };
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {g(0), g(1)}));
        };

        // pow (2-arg)
        math.builtinFuncs["pow"] = [toF32](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            llvm::Function* fn = ctx.module.getFunction("powf");
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(ctx.builder.getFloatTy(),
                    {ctx.builder.getFloatTy(), ctx.builder.getFloatTy()}, false),
                llvm::Function::ExternalLinkage, "powf", ctx.module);
            auto g = [&](int i) { return args.size()>(size_t)i ? toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]))) : llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0); };
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {g(0), g(1)}));
        };

        // fmod
        math.builtinFuncs["fmod"] = [toF32](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            llvm::Function* fn = ctx.module.getFunction("fmodf");
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(ctx.builder.getFloatTy(),
                    {ctx.builder.getFloatTy(), ctx.builder.getFloatTy()}, false),
                llvm::Function::ExternalLinkage, "fmodf", ctx.module);
            auto g = [&](int i) { return args.size()>(size_t)i ? toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]))) : llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0); };
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {g(0), g(1)}));
        };

        // fract = x - floor(x)
        math.builtinFuncs["fract"] = [toF32](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            llvm::Value* v = args.empty() ? llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0)
                                          : toF32(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])));
            llvm::Function* flFn = ctx.module.getFunction("floorf");
            if (!flFn) flFn = llvm::Function::Create(
                llvm::FunctionType::get(ctx.builder.getFloatTy(),{ctx.builder.getFloatTy()},false),
                llvm::Function::ExternalLinkage,"floorf",ctx.module);
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
            llvm::Function* fn = ctx.module.getFunction(cName);
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(ctx.builder.getFloatTy(),
                    {op, ctx.builder.getInt32Ty()}, false),
                llvm::Function::ExternalLinkage, cName, ctx.module);
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
            llvm::Function* fn = ctx.module.getFunction(fnName);
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(ctx.builder.getVoidTy(),{opty,opty,ctx.builder.getFloatTy(),opty},false),
                llvm::Function::ExternalLinkage, fnName, ctx.module);
            llvm::Value* pa = storeToTmp(ctx,a,structTy);
            llvm::Value* pb = storeToTmp(ctx,b,structTy);
            llvm::AllocaInst* out = ctx.builder.CreateAlloca(structTy);
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
            llvm::Function* fn = ctx.module.getFunction(nm);
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(ctx.builder.getFloatTy(),{op,op},false),
                llvm::Function::ExternalLinkage, nm, ctx.module);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn,{storeToTmp(ctx,a,st),storeToTmp(ctx,b,st)}));
        };

        // cross
        math.builtinFuncs["cross"] = [vec3Ty, storeToTmp]
                                     (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            if (args.size() < 2) return ctx.ir_gen->wrap(llvm::UndefValue::get(vec3Ty));
            auto op = llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn = ctx.module.getFunction("aiml_math_vec3_cross");
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,op,op},false),
                llvm::Function::ExternalLinkage, "aiml_math_vec3_cross", ctx.module);
            llvm::Value* a=static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* b=static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            llvm::AllocaInst* out=ctx.builder.CreateAlloca(vec3Ty);
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
                    llvm::Function* fn=ctx.module.getFunction(nm);
                    if (!fn) {
                        std::vector<llvm::Type*> pts={op,op};
                        if (isScalar2arg) pts={op,ctx.builder.getFloatTy(),op};
                        fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),pts,false),
                            llvm::Function::ExternalLinkage,nm,ctx.module);
                    }
                    llvm::AllocaInst* out=ctx.builder.CreateAlloca(st);
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
                    llvm::Function* fn=ctx.module.getFunction(nm);
                    if (!fn) {
                        std::vector<llvm::Type*> pts=(args.size()>1)?std::vector<llvm::Type*>{op,op}:std::vector<llvm::Type*>{op};
                        fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getFloatTy(),pts,false),
                            llvm::Function::ExternalLinkage,nm,ctx.module);
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
            llvm::Function* fn=ctx.module.getFunction("aiml_math_vec2_angle");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getFloatTy(),{op},false),
                llvm::Function::ExternalLinkage,"aiml_math_vec2_angle",ctx.module);
            llvm::Value* a=static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn,{storeToTmp(ctx,a,vec2Ty)}));
        };
        // perp (vec2 only)
        math.builtinFuncs["perp"] = [vec2Ty, storeToTmp]
                                    (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            if (args.empty()) return ctx.ir_gen->wrap(llvm::UndefValue::get(vec2Ty));
            auto op=llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn=ctx.module.getFunction("aiml_math_vec2_perp");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,op},false),
                llvm::Function::ExternalLinkage,"aiml_math_vec2_perp",ctx.module);
            llvm::Value* a=static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::AllocaInst* out=ctx.builder.CreateAlloca(vec2Ty);
            ctx.builder.CreateCall(fn,{storeToTmp(ctx,a,vec2Ty),out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(vec2Ty,out));
        };

        // reflect / refract
        math.builtinFuncs["reflect"] = [vec3Ty, storeToTmp]
                                       (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            if (args.size()<2) return ctx.ir_gen->wrap(llvm::UndefValue::get(vec3Ty));
            auto op=llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn=ctx.module.getFunction("aiml_math_vec3_reflect");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,op,op},false),
                llvm::Function::ExternalLinkage,"aiml_math_vec3_reflect",ctx.module);
            llvm::Value* a=static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* b=static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            llvm::AllocaInst* out=ctx.builder.CreateAlloca(vec3Ty);
            ctx.builder.CreateCall(fn,{storeToTmp(ctx,a,vec3Ty),storeToTmp(ctx,b,vec3Ty),out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(vec3Ty,out));
        };
        math.builtinFuncs["refract"] = [vec3Ty, storeToTmp, toF32]
                                        (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            if (args.size()<3) return ctx.ir_gen->wrap(llvm::UndefValue::get(vec3Ty));
            auto op=llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn=ctx.module.getFunction("aiml_math_vec3_refract");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,op,ctx.builder.getFloatTy(),op},false),
                llvm::Function::ExternalLinkage,"aiml_math_vec3_refract",ctx.module);
            llvm::Value* a=static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            llvm::Value* b=static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]));
            llvm::Value* ior=toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[2])));
            llvm::AllocaInst* out=ctx.builder.CreateAlloca(vec3Ty);
            ctx.builder.CreateCall(fn,{storeToTmp(ctx,a,vec3Ty),storeToTmp(ctx,b,vec3Ty),ior,out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(vec3Ty,out));
        };

        // ── Matrix functions ──────────────────────────────────────────────
        auto mathMat1V3Out = [&](const std::string& aimlNm, const char* cNm) {
            math.builtinFuncs[aimlNm] = [cNm, mat4Ty, vec3Ty, storeToTmp]
                (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                auto op=llvm::PointerType::get(ctx.context,0);
                llvm::Function* fn=ctx.module.getFunction(cNm);
                if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,op},false),
                    llvm::Function::ExternalLinkage,cNm,ctx.module);
                llvm::AllocaInst* out=ctx.builder.CreateAlloca(mat4Ty);
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
                llvm::Function* fn=ctx.module.getFunction(cNm);
                if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{ctx.builder.getFloatTy(),op},false),
                    llvm::Function::ExternalLinkage,cNm,ctx.module);
                llvm::AllocaInst* out=ctx.builder.CreateAlloca(mat4Ty);
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
            llvm::Function* fn=ctx.module.getFunction("aiml_math_mat4_rotate");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,ctx.builder.getFloatTy(),op},false),
                llvm::Function::ExternalLinkage,"aiml_math_mat4_rotate",ctx.module);
            llvm::AllocaInst* out=ctx.builder.CreateAlloca(mat4Ty);
            llvm::Value* axis=args.size()>0?static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])):llvm::UndefValue::get(vec3Ty);
            llvm::Value* ang =args.size()>1?toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))):llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0);
            ctx.builder.CreateCall(fn,{storeToTmp(ctx,axis,vec3Ty),ang,out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(mat4Ty,out));
        };

        // mat4_perspective(fov, aspect, near, far)
        math.builtinFuncs["mat4_perspective"] = [mat4Ty, toF32]
            (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto op=llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn=ctx.module.getFunction("aiml_math_mat4_perspective");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),
                {ctx.builder.getFloatTy(),ctx.builder.getFloatTy(),ctx.builder.getFloatTy(),ctx.builder.getFloatTy(),op},false),
                llvm::Function::ExternalLinkage,"aiml_math_mat4_perspective",ctx.module);
            auto g=[&](int i){return args.size()>(size_t)i?toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]))):llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0);};
            llvm::AllocaInst* out=ctx.builder.CreateAlloca(mat4Ty);
            ctx.builder.CreateCall(fn,{g(0),g(1),g(2),g(3),out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(mat4Ty,out));
        };

        // mat4_ortho(l, r, b, t, near, far)
        math.builtinFuncs["mat4_ortho"] = [mat4Ty, toF32]
            (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto op=llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn=ctx.module.getFunction("aiml_math_mat4_ortho");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),
                {ctx.builder.getFloatTy(),ctx.builder.getFloatTy(),ctx.builder.getFloatTy(),ctx.builder.getFloatTy(),ctx.builder.getFloatTy(),ctx.builder.getFloatTy(),op},false),
                llvm::Function::ExternalLinkage,"aiml_math_mat4_ortho",ctx.module);
            auto g=[&](int i){return args.size()>(size_t)i?toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]))):llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0);};
            llvm::AllocaInst* out=ctx.builder.CreateAlloca(mat4Ty);
            ctx.builder.CreateCall(fn,{g(0),g(1),g(2),g(3),g(4),g(5),out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(mat4Ty,out));
        };

        // mat4_lookat(eye, center, up)
        math.builtinFuncs["mat4_lookat"] = [mat4Ty, vec3Ty, storeToTmp]
            (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto op=llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn=ctx.module.getFunction("aiml_math_mat4_lookat");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,op,op,op},false),
                llvm::Function::ExternalLinkage,"aiml_math_mat4_lookat",ctx.module);
            auto gv=[&](int i)->llvm::Value*{return args.size()>(size_t)i?static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i])):llvm::UndefValue::get(vec3Ty);};
            llvm::AllocaInst* out=ctx.builder.CreateAlloca(mat4Ty);
            ctx.builder.CreateCall(fn,{storeToTmp(ctx,gv(0),vec3Ty),storeToTmp(ctx,gv(1),vec3Ty),storeToTmp(ctx,gv(2),vec3Ty),out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(mat4Ty,out));
        };

        // mat4_inverse / mat4_transpose
        auto mathMat1MatOut = [&](const std::string& aimlNm, const char* cNm) {
            math.builtinFuncs[aimlNm] = [cNm, mat4Ty, storeToTmp]
                (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                auto op=llvm::PointerType::get(ctx.context,0);
                llvm::Function* fn=ctx.module.getFunction(cNm);
                if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,op},false),
                    llvm::Function::ExternalLinkage,cNm,ctx.module);
                llvm::AllocaInst* out=ctx.builder.CreateAlloca(mat4Ty);
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
            llvm::Function* fn=ctx.module.getFunction("aiml_math_mat4_to_buffer");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(op,{op},false),
                llvm::Function::ExternalLinkage,"aiml_math_mat4_to_buffer",ctx.module);
            if (args.empty()) return ctx.ir_gen->wrap(llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(op)));
            llvm::Value* m=static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]));
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn,{storeToTmp(ctx,m,mat4Ty)}));
        };

        // mat4_to_quat
        math.builtinFuncs["mat4_to_quat"] = [mat4Ty, quatTy, storeToTmp]
            (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto op=llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn=ctx.module.getFunction("aiml_math_mat4_to_quat");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,op},false),
                llvm::Function::ExternalLinkage,"aiml_math_mat4_to_quat",ctx.module);
            llvm::AllocaInst* out=ctx.builder.CreateAlloca(quatTy);
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
            llvm::Function* fn=ctx.module.getFunction("aiml_math_quat_axis_angle");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,ctx.builder.getFloatTy(),op},false),
                llvm::Function::ExternalLinkage,"aiml_math_quat_axis_angle",ctx.module);
            llvm::AllocaInst* out=ctx.builder.CreateAlloca(quatTy);
            llvm::Value* axis=args.size()>0?static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])):llvm::UndefValue::get(vec3Ty);
            llvm::Value* ang =args.size()>1?toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1]))):llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0);
            ctx.builder.CreateCall(fn,{storeToTmp(ctx,axis,vec3Ty),ang,out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(quatTy,out));
        };

        // quat_euler(x, y, z)
        math.builtinFuncs["quat_euler"] = [quatTy, toF32]
            (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto op=llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn=ctx.module.getFunction("aiml_math_quat_euler");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),
                {ctx.builder.getFloatTy(),ctx.builder.getFloatTy(),ctx.builder.getFloatTy(),op},false),
                llvm::Function::ExternalLinkage,"aiml_math_quat_euler",ctx.module);
            auto g=[&](int i){return args.size()>(size_t)i?toF32(ctx,static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[i]))):llvm::ConstantFP::get(ctx.builder.getFloatTy(),0.0);};
            llvm::AllocaInst* out=ctx.builder.CreateAlloca(quatTy);
            ctx.builder.CreateCall(fn,{g(0),g(1),g(2),out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(quatTy,out));
        };

        // quat_mul / quat_normalize / quat_inverse / quat_to_mat4
        auto quatBin = [&](const std::string& nm, const char* cn) {
            math.builtinFuncs[nm] = [cn, quatTy, storeToTmp]
                (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
                auto op=llvm::PointerType::get(ctx.context,0);
                llvm::Function* fn=ctx.module.getFunction(cn);
                if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,op,op},false),
                    llvm::Function::ExternalLinkage,cn,ctx.module);
                llvm::AllocaInst* out=ctx.builder.CreateAlloca(quatTy);
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
                llvm::Function* fn=ctx.module.getFunction(cn);
                if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,op},false),
                    llvm::Function::ExternalLinkage,cn,ctx.module);
                llvm::AllocaInst* out=ctx.builder.CreateAlloca(quatTy);
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
            llvm::Function* fn=ctx.module.getFunction("aiml_math_quat_rotate_vec3");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,op,op},false),
                llvm::Function::ExternalLinkage,"aiml_math_quat_rotate_vec3",ctx.module);
            llvm::AllocaInst* out=ctx.builder.CreateAlloca(vec3Ty);
            llvm::Value* q=args.size()>0?static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])):llvm::UndefValue::get(quatTy);
            llvm::Value* v=args.size()>1?static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])):llvm::UndefValue::get(vec3Ty);
            ctx.builder.CreateCall(fn,{storeToTmp(ctx,q,quatTy),storeToTmp(ctx,v,vec3Ty),out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(vec3Ty,out));
        };

        // quat_to_mat4
        math.builtinFuncs["quat_to_mat4"] = [quatTy, mat4Ty, storeToTmp]
            (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto op=llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn=ctx.module.getFunction("aiml_math_quat_to_mat4");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,op},false),
                llvm::Function::ExternalLinkage,"aiml_math_quat_to_mat4",ctx.module);
            llvm::AllocaInst* out=ctx.builder.CreateAlloca(mat4Ty);
            llvm::Value* q=args.size()>0?static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])):llvm::UndefValue::get(quatTy);
            ctx.builder.CreateCall(fn,{storeToTmp(ctx,q,quatTy),out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(mat4Ty,out));
        };

        // quat_slerp
        math.builtinFuncs["quat_slerp"] = [quatTy, storeToTmp, toF32]
            (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto op=llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn=ctx.module.getFunction("aiml_math_quat_slerp");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,op,ctx.builder.getFloatTy(),op},false),
                llvm::Function::ExternalLinkage,"aiml_math_quat_slerp",ctx.module);
            llvm::AllocaInst* out=ctx.builder.CreateAlloca(quatTy);
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
            llvm::Function* fn=ctx.module.getFunction("aiml_math_ivec2_div");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,ctx.builder.getInt32Ty(),op},false),
                llvm::Function::ExternalLinkage,"aiml_math_ivec2_div",ctx.module);
            llvm::AllocaInst* out=ctx.builder.CreateAlloca(ivec2Ty);
            llvm::Value* a=args.size()>0?static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])):llvm::UndefValue::get(ivec2Ty);
            llvm::Value* s=args.size()>1?static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])):ctx.builder.getInt32(1);
            if (s->getType()->isFloatTy()||s->getType()->isDoubleTy()) s=ctx.builder.CreateFPToSI(s,ctx.builder.getInt32Ty());
            ctx.builder.CreateCall(fn,{storeToTmp(ctx,a,ivec2Ty),s,out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(ivec2Ty,out));
        };
        math.builtinFuncs["iadd"] = [ivec2Ty, storeToTmp]
            (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto op=llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn=ctx.module.getFunction("aiml_math_ivec2_add");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,op,op},false),
                llvm::Function::ExternalLinkage,"aiml_math_ivec2_add",ctx.module);
            llvm::AllocaInst* out=ctx.builder.CreateAlloca(ivec2Ty);
            llvm::Value* a=args.size()>0?static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])):llvm::UndefValue::get(ivec2Ty);
            llvm::Value* b=args.size()>1?static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])):a;
            ctx.builder.CreateCall(fn,{storeToTmp(ctx,a,ivec2Ty),storeToTmp(ctx,b,ivec2Ty),out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(ivec2Ty,out));
        };
        math.builtinFuncs["ivec2_to_vec2"] = [ivec2Ty, vec2Ty, storeToTmp]
            (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto op=llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn=ctx.module.getFunction("aiml_math_ivec2_to_vec2");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,op},false),
                llvm::Function::ExternalLinkage,"aiml_math_ivec2_to_vec2",ctx.module);
            llvm::AllocaInst* out=ctx.builder.CreateAlloca(vec2Ty);
            llvm::Value* a=args.size()>0?static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])):llvm::UndefValue::get(ivec2Ty);
            ctx.builder.CreateCall(fn,{storeToTmp(ctx,a,ivec2Ty),out});
            return ctx.ir_gen->wrap(ctx.builder.CreateLoad(vec2Ty,out));
        };
        math.builtinFuncs["vec2_to_ivec2"] = [ivec2Ty, vec2Ty, storeToTmp]
            (CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto op=llvm::PointerType::get(ctx.context,0);
            llvm::Function* fn=ctx.module.getFunction("aiml_math_vec2_to_ivec2");
            if (!fn) fn=llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(),{op,op},false),
                llvm::Function::ExternalLinkage,"aiml_math_vec2_to_ivec2",ctx.module);
            llvm::AllocaInst* out=ctx.builder.CreateAlloca(ivec2Ty);
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
            llvm::Function* fn = ctx.module.getFunction(name);
            if (!fn)
                fn = llvm::Function::Create(
                    llvm::FunctionType::get(llvm::PointerType::get(ctx.context,0),
                                            {llvm::PointerType::get(ctx.context,0)}, false),
                    llvm::Function::ExternalLinkage, name, ctx.module);
            return fn;
        };
        /* Helper: declare or get (ptr,ptr→ptr) function for 2-arg hash functions */
        auto declHash2 = [&](const char* name) -> llvm::Function* {
            llvm::Function* fn = ctx.module.getFunction(name);
            auto* p = llvm::PointerType::get(ctx.context,0);
            if (!fn)
                fn = llvm::Function::Create(
                    llvm::FunctionType::get(p, {p, p}, false),
                    llvm::Function::ExternalLinkage, name, ctx.module);
            return fn;
        };

#define REG_HASH1(aimlName, cName) \
        hash.builtinFuncs[aimlName] = [declHash1](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* { \
            auto* fn = declHash1(cName); \
            auto* p = llvm::PointerType::get(ctx.context,0); \
            llvm::Value* a = args.size() > 0 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])) \
                                              : llvm::Constant::getNullValue(p); \
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {a})); \
        };

#define REG_HASH2(aimlName, cName) \
        hash.builtinFuncs[aimlName] = [declHash2](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* { \
            auto* fn = declHash2(cName); \
            auto* p = llvm::PointerType::get(ctx.context, 0); \
            llvm::Value* a0 = args.size() > 0 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])) \
                                               : llvm::Constant::getNullValue(p); \
            llvm::Value* a1 = args.size() > 1 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])) \
                                               : llvm::Constant::getNullValue(p); \
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {a0, a1})); \
        };

        REG_HASH1("sha256",        "aiml_hash_sha256")
        REG_HASH1("sha512",        "aiml_hash_sha512")
        REG_HASH1("sha3_256",      "aiml_hash_sha3_256")
        REG_HASH1("keccak256",     "aiml_hash_keccak256")
        REG_HASH1("ripemd160",     "aiml_hash_ripemd160")
        REG_HASH1("blake2b",       "aiml_hash_blake2b")
        REG_HASH1("blake2s",       "aiml_hash_blake2s")
        REG_HASH1("md5",           "aiml_hash_md5")
        REG_HASH1("sha256_raw",    "aiml_hash_sha256_raw")
        REG_HASH1("sha512_raw",    "aiml_hash_sha512_raw")
        REG_HASH1("sha3_256_raw",  "aiml_hash_sha3_256_raw")
        REG_HASH1("keccak256_raw", "aiml_hash_keccak256_raw")
        REG_HASH1("ripemd160_raw", "aiml_hash_ripemd160_raw")
        REG_HASH1("blake2b_raw",   "aiml_hash_blake2b_raw")
        REG_HASH2("hmac_sha256",      "aiml_hash_hmac_sha256")
        REG_HASH2("hmac_sha512",      "aiml_hash_hmac_sha512")
        REG_HASH2("hmac_sha256_raw",  "aiml_hash_hmac_sha256_raw")

#undef REG_HASH1
#undef REG_HASH2

        /* crc32: returns i32 */
        hash.builtinFuncs["crc32"] = [i32t](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p = llvm::PointerType::get(ctx.context, 0);
            llvm::Function* fn = ctx.module.getFunction("aiml_hash_crc32");
            if (!fn)
                fn = llvm::Function::Create(
                    llvm::FunctionType::get(i32t, {p}, false),
                    llvm::Function::ExternalLinkage, "aiml_hash_crc32", ctx.module);
            llvm::Value* a = args.size() > 0 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))
                                             : llvm::Constant::getNullValue(p);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {a}));
        };
    } // end hash namespace

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
            auto* _i64 = ctx.builder.getInt64Ty(); \
            auto* _bt  = ctx.getLLVMType("buffer"); \
            auto* _out = ctx.builder.CreateAlloca(_bt, nullptr, "comp_alloca"); \
            uint64_t _sz = ctx.module.getDataLayout().getTypeAllocSize(_bt); \
            llvm::Function* _mcf = ctx.getFunction("memcpy", _p, {_p, _p, _i64}); \
            ctx.builder.CreateCall(_mcf, {_out, (heapPtr), ctx.builder.getInt64(_sz)}); \
            return _out; \
        }())

        /* Helper: store input buffer<u8> struct value into a temp alloca → AimlCompressBuffer* */
#define COMP_IN_ALLOCA(argVal) \
        ([&]() -> llvm::Value* { \
            auto* _bt  = ctx.getLLVMType("buffer"); \
            auto* _tmp = ctx.builder.CreateAlloca(_bt, nullptr, "decomp_in"); \
            ctx.builder.CreateStore((argVal), _tmp); \
            return _tmp; \
        }())

        /* compress.X(data, [level]): string/buffer<u8> → buffer<u8>
         * Always passes len=-1 so the C runtime uses strlen for string inputs. */
#define REG_COMP(aimlName, cName) \
        compress.builtinFuncs[aimlName] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* { \
            auto* p   = llvm::PointerType::get(ctx.context, 0); \
            auto* i32 = ctx.builder.getInt32Ty(); \
            llvm::Function* fn = ctx.module.getFunction(cName); \
            if (!fn) fn = llvm::Function::Create( \
                llvm::FunctionType::get(p, {p, i32, i32}, false), \
                llvm::Function::ExternalLinkage, cName, ctx.module); \
            llvm::Value* data  = args.size() > 0 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0])) \
                                                 : llvm::Constant::getNullValue(p); \
            llvm::Value* level = args.size() > 1 ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[1])) \
                                                 : ctx.builder.getInt32(0); \
            llvm::Value* heapPtr = ctx.builder.CreateCall(fn, {data, ctx.builder.getInt32(-1), level}); \
            return ctx.ir_gen->wrap(COMP_HEAP_TO_ALLOCA(heapPtr)); \
        };

        /* compress.X_decompress_str(buf): buffer<u8> → string (char*) */
#define REG_DECOMP_STR(aimlName, cName) \
        compress.builtinFuncs[aimlName] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* { \
            auto* p = llvm::PointerType::get(ctx.context, 0); \
            llvm::Function* fn = ctx.module.getFunction(cName); \
            if (!fn) fn = llvm::Function::Create( \
                llvm::FunctionType::get(p, {p}, false), \
                llvm::Function::ExternalLinkage, cName, ctx.module); \
            llvm::Value* inPtr = (args.size() > 0) \
                ? COMP_IN_ALLOCA(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))) \
                : llvm::Constant::getNullValue(p); \
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fn, {inPtr})); \
        };

        /* compress.X_decompress(buf): buffer<u8> → buffer<u8> */
#define REG_DECOMP_BUF(aimlName, cName) \
        compress.builtinFuncs[aimlName] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* { \
            auto* p = llvm::PointerType::get(ctx.context, 0); \
            llvm::Function* fn = ctx.module.getFunction(cName); \
            if (!fn) fn = llvm::Function::Create( \
                llvm::FunctionType::get(p, {p}, false), \
                llvm::Function::ExternalLinkage, cName, ctx.module); \
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
            llvm::Function* fn = ctx.module.getFunction(cName); \
            if (!fn) fn = llvm::Function::Create( \
                llvm::FunctionType::get(i32, {p, p}, false), \
                llvm::Function::ExternalLinkage, cName, ctx.module); \
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
            llvm::Function* fn = ctx.module.getFunction("aiml_compress_lz4");
            if (!fn) fn = llvm::Function::Create(
                llvm::FunctionType::get(p, {p, i32}, false),
                llvm::Function::ExternalLinkage, "aiml_compress_lz4", ctx.module);
            llvm::Value* data = args.size() > 0
                ? static_cast<llvm::Value*>(ctx.ir_gen->unwrap(args[0]))
                : llvm::Constant::getNullValue(p);
            llvm::Value* heapPtr = ctx.builder.CreateCall(fn, {data, ctx.builder.getInt32(-1)});
            return ctx.ir_gen->wrap(COMP_HEAP_TO_ALLOCA(heapPtr));
        };

        REG_ARCHIVE("tar_gz",   "aiml_compress_tar_gz")
        REG_ARCHIVE("untar_gz", "aiml_compress_untar_gz")
        REG_ARCHIVE("zip",      "aiml_compress_zip")
        REG_ARCHIVE("unzip",    "aiml_compress_unzip")

        /* compress.zip_entries(path) → list<ptr<zip_entry>> */
        compress.builtinFuncs["zip_entries"] = [](CompilerContext& ctx, const std::vector<AIML::IRValue*>& args) -> AIML::IRValue* {
            auto* p   = llvm::PointerType::get(ctx.context, 0);
            llvm::StructType* listTy = ListImpl::getListStructType(ctx);
            llvm::Value* listAlloca  = ctx.builder.CreateAlloca(listTy, nullptr, "zip_entries_list");
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
            llvm::Value* listAlloca  = ctx.builder.CreateAlloca(listTy, nullptr, "tar_entries_list");
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
}
