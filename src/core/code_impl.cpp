#define DEBUG_MODE
#include "code_impl.h"
#include "list_impl.h"
#include <llvm/IR/IRBuilder.h>

namespace CodeImpl {

// ---------------------------------------------------------------------------
// Field indices in __aiml_code_t
// { i8* id, i8* start_call, i8* code_chunks, i8* data_chunks }
// ---------------------------------------------------------------------------
enum Field : unsigned { ID = 0, START_CALL = 1, CODE_CHUNKS = 2, DATA_CHUNKS = 3 };

// ---------------------------------------------------------------------------
// getStructType
// ---------------------------------------------------------------------------
llvm::StructType* getStructType(CompilerContext& ctx) {
    if (auto* ty = llvm::StructType::getTypeByName(ctx.context, "__aiml_code_t"))
        return ty;
    llvm::Type* i8p = llvm::Type::getInt8Ty(ctx.context)->getPointerTo();
    return llvm::StructType::create(ctx.context, {i8p, i8p, i8p, i8p}, "__aiml_code_t");
}

// ---------------------------------------------------------------------------
// emitNewFn — __aiml_code_new(i8* id, i8* start_call, i8* code_chunks, i8* data_chunks) -> i8*
// Allocates __aiml_code_t on the heap, stores all 4 fields, returns raw i8*.
// ---------------------------------------------------------------------------
static llvm::Function* emitNewFn(CompilerContext& ctx) {
    if (auto* f = ctx.module.getFunction("__aiml_code_new")) return f;

    llvm::Type*       i8p    = llvm::Type::getInt8Ty(ctx.context)->getPointerTo();
    llvm::Type*       i64    = llvm::Type::getInt64Ty(ctx.context);
    llvm::StructType* codeTy = getStructType(ctx);

    auto savedIP = ctx.builder.saveIP();

    auto* ft = llvm::FunctionType::get(i8p, {i8p, i8p, i8p, i8p}, false);
    auto* fn = llvm::Function::Create(ft, llvm::Function::InternalLinkage,
                                      "__aiml_code_new", ctx.module);
    fn->addFnAttr(llvm::Attribute::NoInline);

    auto* entry = llvm::BasicBlock::Create(ctx.context, "entry", fn);
    ctx.builder.SetInsertPoint(entry);

    llvm::DataLayout dl(&ctx.module);
    uint64_t structBytes = dl.getTypeAllocSize(codeTy);
    llvm::Function* mallocFn = ctx.getFunction("malloc", i8p, {i64});
    llvm::Value* raw     = ctx.builder.CreateCall(mallocFn,
                               {ctx.builder.getInt64(structBytes)}, "code_raw");
    llvm::Value* codePtr = ctx.builder.CreateBitCast(raw, codeTy->getPointerTo(), "code_ptr");

    ctx.builder.CreateStore(fn->getArg(0),
        ctx.builder.CreateStructGEP(codeTy, codePtr, Field::ID,          "id_gep"));
    ctx.builder.CreateStore(fn->getArg(1),
        ctx.builder.CreateStructGEP(codeTy, codePtr, Field::START_CALL,  "sc_gep"));
    ctx.builder.CreateStore(fn->getArg(2),
        ctx.builder.CreateStructGEP(codeTy, codePtr, Field::CODE_CHUNKS, "cc_gep"));
    ctx.builder.CreateStore(fn->getArg(3),
        ctx.builder.CreateStructGEP(codeTy, codePtr, Field::DATA_CHUNKS, "dc_gep"));

    ctx.builder.CreateRet(raw);
    ctx.builder.restoreIP(savedIP);
    return fn;
}

// ---------------------------------------------------------------------------
// emitContainsFn — __aiml_code_contains(i8* code, i8* needle) -> i8
// Delegates to C runtime: int aiml_code_contains(void*, const char*)
// ---------------------------------------------------------------------------
static llvm::Function* emitContainsFn(CompilerContext& ctx) {
    if (auto* f = ctx.module.getFunction("__aiml_code_contains")) return f;

    llvm::Type* i8p = llvm::Type::getInt8Ty(ctx.context)->getPointerTo();
    llvm::Type* i8  = llvm::Type::getInt8Ty(ctx.context);
    llvm::Type* i32 = llvm::Type::getInt32Ty(ctx.context);

    auto savedIP = ctx.builder.saveIP();

    auto* ft = llvm::FunctionType::get(i8, {i8p, i8p}, false);
    auto* fn = llvm::Function::Create(ft, llvm::Function::InternalLinkage,
                                      "__aiml_code_contains", ctx.module);
    fn->addFnAttr(llvm::Attribute::NoInline);

    auto* entry = llvm::BasicBlock::Create(ctx.context, "entry", fn);
    ctx.builder.SetInsertPoint(entry);

    llvm::Function* cFn = ctx.getFunction("aiml_code_contains", i32, {i8p, i8p});
    llvm::Value* res    = ctx.builder.CreateCall(cFn, {fn->getArg(0), fn->getArg(1)}, "res");
    ctx.builder.CreateRet(ctx.builder.CreateTrunc(res, i8, "res_i8"));

    ctx.builder.restoreIP(savedIP);
    return fn;
}

// ---------------------------------------------------------------------------
// emitToStringFn — __aiml_code_tostring(i8* code) -> i8*
// Delegates to C runtime: char* aiml_code_tostring(void*)
// ---------------------------------------------------------------------------
static llvm::Function* emitToStringFn(CompilerContext& ctx) {
    if (auto* f = ctx.module.getFunction("__aiml_code_tostring")) return f;

    llvm::Type* i8p = llvm::Type::getInt8Ty(ctx.context)->getPointerTo();

    auto savedIP = ctx.builder.saveIP();

    auto* ft = llvm::FunctionType::get(i8p, {i8p}, false);
    auto* fn = llvm::Function::Create(ft, llvm::Function::InternalLinkage,
                                      "__aiml_code_tostring", ctx.module);
    fn->addFnAttr(llvm::Attribute::NoInline);

    auto* entry = llvm::BasicBlock::Create(ctx.context, "entry", fn);
    ctx.builder.SetInsertPoint(entry);

    llvm::Function* cFn = ctx.getFunction("aiml_code_tostring", i8p, {i8p});
    llvm::Value* str    = ctx.builder.CreateCall(cFn, {fn->getArg(0)}, "str");
    ctx.builder.CreateRet(str);

    ctx.builder.restoreIP(savedIP);
    return fn;
}

// ---------------------------------------------------------------------------
// emitSizeFn — __aiml_code_size(i8* code) -> i64
// Returns strlen(tostring(code)).
// ---------------------------------------------------------------------------
static llvm::Function* emitSizeFn(CompilerContext& ctx) {
    if (auto* f = ctx.module.getFunction("__aiml_code_size")) return f;

    llvm::Function* toStringFn = emitToStringFn(ctx);

    llvm::Type* i8p = llvm::Type::getInt8Ty(ctx.context)->getPointerTo();
    llvm::Type* i64 = llvm::Type::getInt64Ty(ctx.context);

    auto savedIP = ctx.builder.saveIP();

    auto* ft = llvm::FunctionType::get(i64, {i8p}, false);
    auto* fn = llvm::Function::Create(ft, llvm::Function::InternalLinkage,
                                      "__aiml_code_size", ctx.module);
    fn->addFnAttr(llvm::Attribute::NoInline);

    auto* entry = llvm::BasicBlock::Create(ctx.context, "entry", fn);
    ctx.builder.SetInsertPoint(entry);

    llvm::Value* str        = ctx.builder.CreateCall(toStringFn, {fn->getArg(0)}, "str");
    llvm::Function* strlenFn = ctx.getFunction("strlen", i64, {i8p});
    llvm::Value* len        = ctx.builder.CreateCall(strlenFn, {str}, "len");
    ctx.builder.CreateRet(len);

    ctx.builder.restoreIP(savedIP);
    return fn;
}

// ---------------------------------------------------------------------------
// emitMergeFn — __aiml_code_merge(i8* a, i8* b) -> i8*
// Delegates to C runtime: void* aiml_code_merge(void*, void*)
// ---------------------------------------------------------------------------
static llvm::Function* emitMergeFn(CompilerContext& ctx) {
    if (auto* f = ctx.module.getFunction("__aiml_code_merge")) return f;

    llvm::Type* i8p = llvm::Type::getInt8Ty(ctx.context)->getPointerTo();

    auto savedIP = ctx.builder.saveIP();

    auto* ft = llvm::FunctionType::get(i8p, {i8p, i8p}, false);
    auto* fn = llvm::Function::Create(ft, llvm::Function::InternalLinkage,
                                      "__aiml_code_merge", ctx.module);
    fn->addFnAttr(llvm::Attribute::NoInline);

    auto* entry = llvm::BasicBlock::Create(ctx.context, "entry", fn);
    ctx.builder.SetInsertPoint(entry);

    llvm::Function* cFn    = ctx.getFunction("aiml_code_merge", i8p, {i8p, i8p});
    llvm::Value* merged    = ctx.builder.CreateCall(cFn,
                                {fn->getArg(0), fn->getArg(1)}, "merged");
    ctx.builder.CreateRet(merged);

    ctx.builder.restoreIP(savedIP);
    return fn;
}

// ---------------------------------------------------------------------------
// push_code — inline IR: load code_chunks ptr, push chunk string onto the list
// ---------------------------------------------------------------------------
void push_code(CompilerContext& ctx, llvm::Value* codePtr, llvm::Value* chunk) {
    llvm::Type*       i8p    = llvm::Type::getInt8Ty(ctx.context)->getPointerTo();
    llvm::StructType* codeTy = getStructType(ctx);
    llvm::StructType* listTy = ListImpl::getListStructType(ctx);

    llvm::Value* typed   = ctx.builder.CreateBitCast(codePtr, codeTy->getPointerTo(), "pc_code");
    llvm::Value* ccGEP   = ctx.builder.CreateStructGEP(codeTy, typed, Field::CODE_CHUNKS, "cc_gep");
    llvm::Value* listRaw = ctx.builder.CreateLoad(i8p, ccGEP, "cc_raw");
    llvm::Value* listPtr = ctx.builder.CreateBitCast(listRaw, listTy->getPointerTo(), "cc_list");
    ListImpl::push(ctx, listPtr, chunk);
}

// ---------------------------------------------------------------------------
// push_data — inline IR: load data_chunks ptr, push chunk string onto the list
// ---------------------------------------------------------------------------
void push_data(CompilerContext& ctx, llvm::Value* codePtr, llvm::Value* chunk) {
    llvm::Type*       i8p    = llvm::Type::getInt8Ty(ctx.context)->getPointerTo();
    llvm::StructType* codeTy = getStructType(ctx);
    llvm::StructType* listTy = ListImpl::getListStructType(ctx);

    llvm::Value* typed   = ctx.builder.CreateBitCast(codePtr, codeTy->getPointerTo(), "pd_code");
    llvm::Value* dcGEP   = ctx.builder.CreateStructGEP(codeTy, typed, Field::DATA_CHUNKS, "dc_gep");
    llvm::Value* listRaw = ctx.builder.CreateLoad(i8p, dcGEP, "dc_raw");
    llvm::Value* listPtr = ctx.builder.CreateBitCast(listRaw, listTy->getPointerTo(), "dc_list");
    ListImpl::push(ctx, listPtr, chunk);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
llvm::Value* create(CompilerContext& ctx,
                    llvm::Value* id,
                    llvm::Value* start_call,
                    llvm::Value* code_chunks,
                    llvm::Value* data_chunks) {
    return ctx.builder.CreateCall(emitNewFn(ctx),
                                  {id, start_call, code_chunks, data_chunks},
                                  "code_new");
}

llvm::Value* contains(CompilerContext& ctx, llvm::Value* codePtr, llvm::Value* name) {
    return ctx.builder.CreateCall(emitContainsFn(ctx), {codePtr, name}, "code_contains");
}

llvm::Value* size(CompilerContext& ctx, llvm::Value* codePtr) {
    return ctx.builder.CreateCall(emitSizeFn(ctx), {codePtr}, "code_size");
}

llvm::Value* toString(CompilerContext& ctx, llvm::Value* codePtr) {
    return ctx.builder.CreateCall(emitToStringFn(ctx), {codePtr}, "code_tostring");
}

llvm::Value* merge(CompilerContext& ctx, llvm::Value* aPtr, llvm::Value* bPtr) {
    return ctx.builder.CreateCall(emitMergeFn(ctx), {aPtr, bPtr}, "code_merge");
}

} // namespace CodeImpl
