#include "chan_impl.h"

namespace ChanImpl {

// ── Struct Type ───────────────────────────────────────────────────────────────
// Layout:
//   0: [40 x i8]  mutex
//   1: [48 x i8]  cond_put    (buffered: space available; rendezvous: slot empty)
//   2: [48 x i8]  cond_get    (item available)
//   3: i64        capacity    (0 = rendezvous, N = buffered)
//   4: i64        size        (buffered: items in queue; rendezvous: 0=empty, 1=item deposited)
//   5: i64        head        (buffered: next read index)
//   6: i64        tail        (buffered: next write index)
//   7: i8*        buffer      (element array; always 1 slot for rendezvous)
//   8: [48 x i8]  cond_taken  (rendezvous only: sender waits here for recv ack)
//   9: i64        rc          (spec/61 Class 5 atomic refcount; AimlChanBlock in ser_runtime.cpp)

llvm::StructType* getStructType(CompilerContext& ctx) {
    if (auto* existing = llvm::StructType::getTypeByName(ctx.context, "__aiml_chan_struct"))
        return existing;
    auto* i8  = ctx.builder.getInt8Ty();
    auto* i64 = ctx.builder.getInt64Ty();
    return llvm::StructType::create(ctx.context, {
        llvm::ArrayType::get(i8, 40),   // 0: pthread_mutex_t
        llvm::ArrayType::get(i8, 48),   // 1: cond_put
        llvm::ArrayType::get(i8, 48),   // 2: cond_get
        i64,                            // 3: capacity
        i64,                            // 4: size
        i64,                            // 5: head
        i64,                            // 6: tail
        i8->getPointerTo(),             // 7: buffer (i8*, element array)
        llvm::ArrayType::get(i8, 48),   // 8: cond_taken (rendezvous ack)
        i64,                            // 9: rc
    }, "__aiml_chan_struct");
}

// ── pthread helpers ───────────────────────────────────────────────────────────

static llvm::Function* getMalloc(CompilerContext& ctx) {
    if (auto* f = ctx.module.getFunction("malloc")) return f;
    auto* ty = llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(),
                                       {ctx.builder.getInt64Ty()}, false);
    return llvm::Function::Create(ty, llvm::Function::ExternalLinkage, "malloc", ctx.module);
}
static llvm::Function* getMutexInit(CompilerContext& ctx) {
    if (auto* f = ctx.module.getFunction("pthread_mutex_init")) return f;
    auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
    return llvm::Function::Create(
        llvm::FunctionType::get(ctx.builder.getInt32Ty(), {i8p, i8p}, false),
        llvm::Function::ExternalLinkage, "pthread_mutex_init", ctx.module);
}
static llvm::Function* getCondInit(CompilerContext& ctx) {
    if (auto* f = ctx.module.getFunction("pthread_cond_init")) return f;
    auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
    return llvm::Function::Create(
        llvm::FunctionType::get(ctx.builder.getInt32Ty(), {i8p, i8p}, false),
        llvm::Function::ExternalLinkage, "pthread_cond_init", ctx.module);
}
static llvm::Function* getMutexLock(CompilerContext& ctx) {
    if (auto* f = ctx.module.getFunction("pthread_mutex_lock")) return f;
    auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
    return llvm::Function::Create(
        llvm::FunctionType::get(ctx.builder.getInt32Ty(), {i8p}, false),
        llvm::Function::ExternalLinkage, "pthread_mutex_lock", ctx.module);
}
static llvm::Function* getMutexUnlock(CompilerContext& ctx) {
    if (auto* f = ctx.module.getFunction("pthread_mutex_unlock")) return f;
    auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
    return llvm::Function::Create(
        llvm::FunctionType::get(ctx.builder.getInt32Ty(), {i8p}, false),
        llvm::Function::ExternalLinkage, "pthread_mutex_unlock", ctx.module);
}
static llvm::Function* getCondWait(CompilerContext& ctx) {
    if (auto* f = ctx.module.getFunction("pthread_cond_wait")) return f;
    auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
    return llvm::Function::Create(
        llvm::FunctionType::get(ctx.builder.getInt32Ty(), {i8p, i8p}, false),
        llvm::Function::ExternalLinkage, "pthread_cond_wait", ctx.module);
}
static llvm::Function* getCondSignal(CompilerContext& ctx) {
    if (auto* f = ctx.module.getFunction("pthread_cond_signal")) return f;
    auto* i8p = ctx.builder.getInt8Ty()->getPointerTo();
    return llvm::Function::Create(
        llvm::FunctionType::get(ctx.builder.getInt32Ty(), {i8p}, false),
        llvm::Function::ExternalLinkage, "pthread_cond_signal", ctx.module);
}

// ── Field addresses (all computed in entry BB, dominate all other blocks) ─────

struct ChanFields {
    llvm::Value* mutexI8;
    llvm::Value* condPutI8;
    llvm::Value* condGetI8;
    llvm::Value* capGEP;    // i64*
    llvm::Value* sizeGEP;   // i64*
    llvm::Value* headGEP;   // i64*
    llvm::Value* tailGEP;   // i64*
    llvm::Value* bufGEP;    // i8**
    llvm::Value* condTakenI8;
};

static ChanFields computeFields(llvm::IRBuilder<>& builder, llvm::StructType* structTy,
                                llvm::Value* structPtr) {
    auto* i8p = builder.getInt8Ty()->getPointerTo();
    ChanFields f;
    auto gep = [&](unsigned idx) -> llvm::Value* {
        return builder.CreateInBoundsGEP(structTy, structPtr,
            {builder.getInt32(0), builder.getInt32(idx)});
    };
    f.mutexI8      = builder.CreateBitCast(gep(0), i8p, "mutex_i8");
    f.condPutI8    = builder.CreateBitCast(gep(1), i8p, "cond_put_i8");
    f.condGetI8    = builder.CreateBitCast(gep(2), i8p, "cond_get_i8");
    f.capGEP       = gep(3);
    f.sizeGEP      = gep(4);
    f.headGEP      = gep(5);
    f.tailGEP      = gep(6);
    f.bufGEP       = gep(7);
    f.condTakenI8  = builder.CreateBitCast(gep(8), i8p, "cond_taken_i8");
    return f;
}

// ── __aiml_chan_send_i64 ──────────────────────────────────────────────────────
//
// Buffered (cap > 0):
//   lock → wait while full → store at tail → size++ → signal cond_get → unlock
//
// Rendezvous (cap == 0):
//   lock → wait while slot occupied (size!=0) → store in buf[0] → size=1 →
//   signal cond_get → wait while size!=0 (cond_taken) → unlock

static llvm::Function* emitSendFn(CompilerContext& ctx) {
    if (auto* f = ctx.module.getFunction("__aiml_chan_send_i64")) return f;

    auto& builder  = ctx.builder;
    auto* i8p      = builder.getInt8Ty()->getPointerTo();
    auto* i64Ty    = builder.getInt64Ty();
    auto* structTy = getStructType(ctx);

    auto* fn = llvm::Function::Create(
        llvm::FunctionType::get(builder.getVoidTy(), {i8p, i64Ty}, false),
        llvm::Function::InternalLinkage, "__aiml_chan_send_i64", ctx.module);
    auto* chanArg = fn->getArg(0); chanArg->setName("chan");
    auto* valArg  = fn->getArg(1); valArg->setName("val");

    auto savedIP = builder.saveIP();

    auto bb = [&](const char* name) { return llvm::BasicBlock::Create(ctx.context, name, fn); };
    auto* entryBB      = bb("entry");
    auto* bufCheckBB   = bb("buf_check");
    auto* bufWaitBB    = bb("buf_wait");
    auto* doSendBB     = bb("do_send");
    auto* rvCheckBB    = bb("rv_check");
    auto* rvWaitBB     = bb("rv_wait");
    auto* rvStoreBB    = bb("rv_store");
    auto* rvAckCheckBB = bb("rv_ack_check");
    auto* rvAckWaitBB  = bb("rv_ack_wait");
    auto* rvDoneBB     = bb("rv_done");

    // ── entry ──────────────────────────────────────────────────────────────
    builder.SetInsertPoint(entryBB);
    auto* sp   = builder.CreateBitCast(chanArg, structTy->getPointerTo(), "sp");
    auto f     = computeFields(builder, structTy, sp);
    builder.CreateCall(getMutexLock(ctx), {f.mutexI8});
    auto* cap  = builder.CreateLoad(i64Ty, f.capGEP, "cap");
    auto* isRv = builder.CreateICmpEQ(cap, builder.getInt64(0), "is_rv");
    builder.CreateCondBr(isRv, rvCheckBB, bufCheckBB);

    // ── buffered path ──────────────────────────────────────────────────────
    builder.SetInsertPoint(bufCheckBB);
    auto* size = builder.CreateLoad(i64Ty, f.sizeGEP, "size");
    builder.CreateCondBr(builder.CreateICmpEQ(size, cap, "full"), bufWaitBB, doSendBB);

    builder.SetInsertPoint(bufWaitBB);
    builder.CreateCall(getCondWait(ctx), {f.condPutI8, f.mutexI8});
    builder.CreateBr(bufCheckBB);

    builder.SetInsertPoint(doSendBB);
    {
        auto* tail   = builder.CreateLoad(i64Ty, f.tailGEP, "tail");
        auto* bufRaw = builder.CreateLoad(i8p, f.bufGEP, "buf");
        auto* bufI64 = builder.CreateBitCast(bufRaw, i64Ty->getPointerTo(), "buf_i64");
        builder.CreateStore(valArg, builder.CreateInBoundsGEP(i64Ty, bufI64, tail, "ep"));
        auto* newTail = builder.CreateURem(builder.CreateAdd(tail, builder.getInt64(1)), cap);
        builder.CreateStore(newTail, f.tailGEP);
        auto* sz2 = builder.CreateLoad(i64Ty, f.sizeGEP, "sz2");
        builder.CreateStore(builder.CreateAdd(sz2, builder.getInt64(1)), f.sizeGEP);
        builder.CreateCall(getCondSignal(ctx), {f.condGetI8});
        builder.CreateCall(getMutexUnlock(ctx), {f.mutexI8});
        builder.CreateRetVoid();
    }

    // ── rendezvous path ────────────────────────────────────────────────────
    // Phase 1: wait until slot is empty, then deposit
    builder.SetInsertPoint(rvCheckBB);
    {
        auto* sz = builder.CreateLoad(i64Ty, f.sizeGEP, "rv_sz");
        builder.CreateCondBr(builder.CreateICmpNE(sz, builder.getInt64(0), "occ"), rvWaitBB, rvStoreBB);
    }

    builder.SetInsertPoint(rvWaitBB);
    builder.CreateCall(getCondWait(ctx), {f.condPutI8, f.mutexI8});
    builder.CreateBr(rvCheckBB);

    builder.SetInsertPoint(rvStoreBB);
    {
        auto* bufRaw = builder.CreateLoad(i8p, f.bufGEP, "buf2");
        auto* slot   = builder.CreateInBoundsGEP(i64Ty,
                           builder.CreateBitCast(bufRaw, i64Ty->getPointerTo()), builder.getInt64(0));
        builder.CreateStore(valArg, slot);
        builder.CreateStore(builder.getInt64(1), f.sizeGEP);
        builder.CreateCall(getCondSignal(ctx), {f.condGetI8});
        builder.CreateBr(rvAckCheckBB);
    }

    // Phase 2: wait until receiver acknowledges (size back to 0)
    builder.SetInsertPoint(rvAckCheckBB);
    {
        auto* sz = builder.CreateLoad(i64Ty, f.sizeGEP, "ack_sz");
        builder.CreateCondBr(builder.CreateICmpNE(sz, builder.getInt64(0)), rvAckWaitBB, rvDoneBB);
    }

    builder.SetInsertPoint(rvAckWaitBB);
    builder.CreateCall(getCondWait(ctx), {f.condTakenI8, f.mutexI8});
    builder.CreateBr(rvAckCheckBB);

    builder.SetInsertPoint(rvDoneBB);
    builder.CreateCall(getMutexUnlock(ctx), {f.mutexI8});
    builder.CreateRetVoid();

    builder.restoreIP(savedIP);
    return fn;
}

// ── __aiml_chan_recv_i64 ──────────────────────────────────────────────────────
//
// Buffered (cap > 0):
//   lock → wait while empty → load from head → size-- → signal cond_put → unlock → ret
//
// Rendezvous (cap == 0):
//   lock → wait while size==0 → load from buf[0] → size=0 →
//   signal cond_taken + cond_put → unlock → ret

static llvm::Function* emitRecvFn(CompilerContext& ctx) {
    if (auto* f = ctx.module.getFunction("__aiml_chan_recv_i64")) return f;

    auto& builder  = ctx.builder;
    auto* i8p      = builder.getInt8Ty()->getPointerTo();
    auto* i64Ty    = builder.getInt64Ty();
    auto* structTy = getStructType(ctx);

    auto* fn = llvm::Function::Create(
        llvm::FunctionType::get(i64Ty, {i8p}, false),
        llvm::Function::InternalLinkage, "__aiml_chan_recv_i64", ctx.module);
    auto* chanArg = fn->getArg(0); chanArg->setName("chan");

    auto savedIP = builder.saveIP();

    auto bb = [&](const char* name) { return llvm::BasicBlock::Create(ctx.context, name, fn); };
    auto* entryBB    = bb("entry");
    auto* bufCheckBB = bb("buf_check");
    auto* bufWaitBB  = bb("buf_wait");
    auto* doRecvBB   = bb("do_recv");
    auto* rvCheckBB  = bb("rv_check");
    auto* rvWaitBB   = bb("rv_wait");
    auto* rvTakeBB   = bb("rv_take");

    // ── entry ──────────────────────────────────────────────────────────────
    builder.SetInsertPoint(entryBB);
    auto* sp   = builder.CreateBitCast(chanArg, structTy->getPointerTo(), "sp");
    auto f     = computeFields(builder, structTy, sp);
    builder.CreateCall(getMutexLock(ctx), {f.mutexI8});
    auto* cap  = builder.CreateLoad(i64Ty, f.capGEP, "cap");
    auto* isRv = builder.CreateICmpEQ(cap, builder.getInt64(0), "is_rv");
    builder.CreateCondBr(isRv, rvCheckBB, bufCheckBB);

    // ── buffered path ──────────────────────────────────────────────────────
    builder.SetInsertPoint(bufCheckBB);
    {
        auto* sz = builder.CreateLoad(i64Ty, f.sizeGEP, "size");
        builder.CreateCondBr(builder.CreateICmpEQ(sz, builder.getInt64(0), "empty"), bufWaitBB, doRecvBB);
    }

    builder.SetInsertPoint(bufWaitBB);
    builder.CreateCall(getCondWait(ctx), {f.condGetI8, f.mutexI8});
    builder.CreateBr(bufCheckBB);

    builder.SetInsertPoint(doRecvBB);
    {
        auto* head   = builder.CreateLoad(i64Ty, f.headGEP, "head");
        auto* bufRaw = builder.CreateLoad(i8p, f.bufGEP, "buf");
        auto* bufI64 = builder.CreateBitCast(bufRaw, i64Ty->getPointerTo(), "buf_i64");
        auto* val    = builder.CreateLoad(i64Ty,
                           builder.CreateInBoundsGEP(i64Ty, bufI64, head, "ep"), "val");
        auto* newHd  = builder.CreateURem(builder.CreateAdd(head, builder.getInt64(1)), cap);
        builder.CreateStore(newHd, f.headGEP);
        auto* sz2    = builder.CreateLoad(i64Ty, f.sizeGEP, "sz2");
        builder.CreateStore(builder.CreateSub(sz2, builder.getInt64(1)), f.sizeGEP);
        builder.CreateCall(getCondSignal(ctx), {f.condPutI8});
        builder.CreateCall(getMutexUnlock(ctx), {f.mutexI8});
        builder.CreateRet(val);
    }

    // ── rendezvous path ────────────────────────────────────────────────────
    builder.SetInsertPoint(rvCheckBB);
    {
        auto* sz = builder.CreateLoad(i64Ty, f.sizeGEP, "rv_sz");
        builder.CreateCondBr(builder.CreateICmpEQ(sz, builder.getInt64(0), "empty"), rvWaitBB, rvTakeBB);
    }

    builder.SetInsertPoint(rvWaitBB);
    builder.CreateCall(getCondWait(ctx), {f.condGetI8, f.mutexI8});
    builder.CreateBr(rvCheckBB);

    builder.SetInsertPoint(rvTakeBB);
    {
        auto* bufRaw = builder.CreateLoad(i8p, f.bufGEP, "buf2");
        auto* slot   = builder.CreateInBoundsGEP(i64Ty,
                           builder.CreateBitCast(bufRaw, i64Ty->getPointerTo()), builder.getInt64(0));
        auto* val    = builder.CreateLoad(i64Ty, slot, "val2");
        builder.CreateStore(builder.getInt64(0), f.sizeGEP);     // ack: slot now empty
        builder.CreateCall(getCondSignal(ctx), {f.condTakenI8}); // wake sender
        builder.CreateCall(getCondSignal(ctx), {f.condPutI8});   // wake next sender (if any)
        builder.CreateCall(getMutexUnlock(ctx), {f.mutexI8});
        builder.CreateRet(val);
    }

    builder.restoreIP(savedIP);
    return fn;
}

// ── Public API ────────────────────────────────────────────────────────────────

void init(CompilerContext& ctx, llvm::Value* chanAlloca, llvm::Value* capacity) {
    auto& builder  = ctx.builder;
    auto* i8p      = builder.getInt8Ty()->getPointerTo();
    auto* i64Ty    = builder.getInt64Ty();
    auto* structTy = getStructType(ctx);
    auto* null8p   = llvm::ConstantPointerNull::get(i8p);

    uint64_t structSize = ctx.module.getDataLayout().getTypeAllocSize(structTy);
    auto* rawPtr    = builder.CreateCall(getMalloc(ctx), {builder.getInt64(structSize)}, "chan_raw");
    auto* structPtr = builder.CreateBitCast(rawPtr, structTy->getPointerTo(), "chan_struct");
    builder.CreateStore(rawPtr, chanAlloca);

    auto gep = [&](unsigned idx) -> llvm::Value* {
        return builder.CreateInBoundsGEP(structTy, structPtr,
            {builder.getInt32(0), builder.getInt32(idx)});
    };

    builder.CreateCall(getMutexInit(ctx), {builder.CreateBitCast(gep(0), i8p), null8p}); // mutex
    builder.CreateCall(getCondInit(ctx),  {builder.CreateBitCast(gep(1), i8p), null8p}); // cond_put
    builder.CreateCall(getCondInit(ctx),  {builder.CreateBitCast(gep(2), i8p), null8p}); // cond_get
    builder.CreateCall(getCondInit(ctx),  {builder.CreateBitCast(gep(8), i8p), null8p}); // cond_taken

    builder.CreateStore(capacity,            gep(3)); // capacity
    builder.CreateStore(builder.getInt64(0), gep(4)); // size
    builder.CreateStore(builder.getInt64(0), gep(5)); // head
    builder.CreateStore(builder.getInt64(0), gep(6)); // tail
    builder.CreateStore(builder.getInt64(1), gep(9)); // rc

    // Buffer: rendezvous needs 1 handoff slot; buffered needs cap slots
    auto* isRv  = builder.CreateICmpEQ(capacity, builder.getInt64(0), "is_rv");
    auto* slots = builder.CreateSelect(isRv, builder.getInt64(1), capacity, "slots");
    auto* bufSz = builder.CreateMul(slots, builder.getInt64(8), "buf_size");
    builder.CreateStore(builder.CreateCall(getMalloc(ctx), {bufSz}, "buf"), gep(7));
}

void send(CompilerContext& ctx, llvm::Value* chanAlloca, llvm::Value* val, llvm::Type* elemType) {
    auto& builder = ctx.builder;
    auto* i8p     = builder.getInt8Ty()->getPointerTo();
    auto* i64Ty   = builder.getInt64Ty();
    auto* chanPtr = builder.CreateLoad(i8p, chanAlloca, "chan_ptr");

    llvm::Value* valI64;
    if      (elemType->isIntegerTy(64)) valI64 = val;
    else if (elemType->isPointerTy())   valI64 = builder.CreatePtrToInt(val, i64Ty, "vi64");
    else if (elemType->isIntegerTy())   valI64 = builder.CreateZExt(val, i64Ty, "vi64");
    else                                valI64 = builder.CreateBitCast(val, i64Ty, "vi64");

    builder.CreateCall(emitSendFn(ctx), {chanPtr, valI64});
}

llvm::Value* recv(CompilerContext& ctx, llvm::Value* chanAlloca, llvm::Type* elemType) {
    auto& builder = ctx.builder;
    auto* i8p     = builder.getInt8Ty()->getPointerTo();
    auto* chanPtr = builder.CreateLoad(i8p, chanAlloca, "chan_ptr");
    auto* valI64  = builder.CreateCall(emitRecvFn(ctx), {chanPtr}, "recv_i64");

    if      (elemType->isIntegerTy(64)) return valI64;
    else if (elemType->isPointerTy())   return builder.CreateIntToPtr(valI64, elemType, "recv_ptr");
    else if (elemType->isIntegerTy())   return builder.CreateTrunc(valI64, elemType, "recv_val");
    else                                return builder.CreateBitCast(valI64, elemType, "recv_val");
}

} // namespace ChanImpl
