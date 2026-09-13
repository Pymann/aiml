#include "list_impl.h"
#include <iostream>

namespace ListImpl {

    llvm::StructType* getListStructType(CompilerContext& ctx) {
        // Look up or create "GenericList" in THIS context.
        // NOTE: Do NOT use a static variable here — each LLVMContext is independent
        // (separate sub-compiler instances each have their own context), so a static
        // pointer from a previous context would be a dangling reference.
        llvm::StructType* t = llvm::StructType::getTypeByName(ctx.context, "GenericList");
        if (!t) {
            t = llvm::StructType::create(ctx.context, {
                ctx.builder.getInt32Ty(), // size
                ctx.builder.getInt32Ty(), // capacity
                llvm::PointerType::get(ctx.context, 0) // data (opaque ptr)
            }, "GenericList");
        }
        return t;
    }

    void initList(CompilerContext& ctx, llvm::Value* listPtr, llvm::Type* elemType, int initialCapacity) {
        llvm::Type* sz  = ctx.getSizeType();
        llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);

        llvm::StructType* structType = getListStructType(ctx);

        // 1. Set Size = 0
        llvm::Value* sizePtr = ctx.builder.CreateStructGEP(structType, listPtr, 0, "sizePtr");
        ctx.builder.CreateStore(ctx.builder.getInt32(0), sizePtr);

        // 2. Set Capacity = initialCapacity
        llvm::Value* capPtr = ctx.builder.CreateStructGEP(structType, listPtr, 1, "capPtr");
        ctx.builder.CreateStore(ctx.builder.getInt32(initialCapacity), capPtr);

        // 3. Malloc Data. All size arithmetic uses size_t (i64 on x86_64,
        // i32 on wasm32) so the malloc call signature matches. The
        // earlier hardcoded i64 silently produced an `unreachable` stub
        // on wasm32 because the malloc arg type didn't match the
        // declaration. Same class of bug as the 2026-05-15 memcpy fix.
        llvm::Value* onePtr = ctx.builder.CreateConstInBoundsGEP1_32(
            elemType, llvm::Constant::getNullValue(elemType->getPointerTo()), 1);
        llvm::Value* elemSize = ctx.builder.CreatePtrToInt(onePtr, sz);

        llvm::Value* sizeVal = ctx.builder.getInt32(initialCapacity);
        llvm::Value* totalSize = ctx.builder.CreateMul(
            elemSize, ctx.builder.CreateZExtOrTrunc(sizeVal, sz));

        llvm::Function* mallocFunc = ctx.getFunction("malloc", ptrTy, {sz});
        llvm::Value* dataVoidPtr = ctx.builder.CreateCall(mallocFunc, {totalSize});

        llvm::Value* dataPtrMember = ctx.builder.CreateStructGEP(structType, listPtr, 2, "dataPtr");
        ctx.builder.CreateStore(dataVoidPtr, dataPtrMember);
    }
    
    void push(CompilerContext& ctx, llvm::Value* listPtr, llvm::Value* val) {
        llvm::StructType* structType = getListStructType(ctx);
        
        llvm::Value* sizePtr = ctx.builder.CreateStructGEP(structType, listPtr, 0, "sizePtr");
        llvm::Value* capPtr = ctx.builder.CreateStructGEP(structType, listPtr, 1, "capPtr");
        
        llvm::Value* sizeVal = ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), sizePtr, "size");
        llvm::Value* capVal = ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), capPtr, "capacity");
        
        // Check if Size == Capacity
        llvm::Function* parentFunc = ctx.builder.GetInsertBlock()->getParent();
        llvm::BasicBlock* growBB = llvm::BasicBlock::Create(ctx.context, "grow", parentFunc);
        llvm::BasicBlock* contBB = llvm::BasicBlock::Create(ctx.context, "cont", parentFunc);
        
        llvm::Value* isFull = ctx.builder.CreateICmpEQ(sizeVal, capVal);
        ctx.builder.CreateCondBr(isFull, growBB, contBB);
        
        // Grow Block
        ctx.builder.SetInsertPoint(growBB);
        {
            llvm::Value* doubled = ctx.builder.CreateMul(capVal, ctx.builder.getInt32(2));
            llvm::Value* minCap = ctx.builder.getInt32(4);
            llvm::Value* newCap = ctx.builder.CreateSelect(
                ctx.builder.CreateICmpSGT(doubled, ctx.builder.getInt32(0)), doubled, minCap);
            ctx.builder.CreateStore(newCap, capPtr);
            
            llvm::Value* dataPtrMember = ctx.builder.CreateStructGEP(structType, listPtr, 2, "dataPtr");
            llvm::Value* oldData = ctx.builder.CreateLoad(llvm::PointerType::get(ctx.context, 0), dataPtrMember);
            
            llvm::Type* sz = ctx.getSizeType();
            llvm::Type* elemType = val->getType();
            llvm::Value* onePtr = ctx.builder.CreateConstInBoundsGEP1_32(elemType, llvm::Constant::getNullValue(elemType->getPointerTo()), 1);
            llvm::Value* elemSize = ctx.builder.CreatePtrToInt(onePtr, sz);
            llvm::Value* totalNewSize = ctx.builder.CreateMul(elemSize, ctx.builder.CreateZExtOrTrunc(newCap, sz));

            llvm::Function* reallocFunc = ctx.getFunction("realloc", llvm::PointerType::get(ctx.context, 0),
                {llvm::PointerType::get(ctx.context, 0), sz});

            llvm::Value* newData = ctx.builder.CreateCall(reallocFunc, {oldData, totalNewSize});
            ctx.builder.CreateStore(newData, dataPtrMember);
            
            ctx.builder.CreateBr(contBB);
        }
        
        // Continue Block
        ctx.builder.SetInsertPoint(contBB);
        
        llvm::Value* dataPtrMember = ctx.builder.CreateStructGEP(structType, listPtr, 2, "dataPtr");
        llvm::Value* dataBase = ctx.builder.CreateLoad(llvm::PointerType::get(ctx.context, 0), dataPtrMember, "dataBase");
        
        llvm::Value* destPtr = ctx.builder.CreateInBoundsGEP(val->getType(), dataBase, sizeVal);
        ctx.builder.CreateStore(val, destPtr);
        
        llvm::Value* newSize = ctx.builder.CreateAdd(sizeVal, ctx.builder.getInt32(1));
        ctx.builder.CreateStore(newSize, sizePtr);
    }

    void pushDeepCopyModel(CompilerContext& ctx, llvm::Value* listPtr, llvm::Value* srcAddr,
                           llvm::Type* elemType, llvm::Value* serNodes, int serCount) {
        // Append a zero-initialised slot (reuse push's grow/append), then deep-copy the source
        // model into it. The zeroed slot satisfies aiml_ser_deep_copy's "dst all-NULL" precondition.
        push(ctx, listPtr, llvm::Constant::getNullValue(elemType));
        llvm::StructType* st = getListStructType(ctx);
        llvm::Value* sizePtr = ctx.builder.CreateStructGEP(st, listPtr, 0, "sizePtr");
        llvm::Value* sizeNow = ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), sizePtr, "size");
        llvm::Value* idx = ctx.builder.CreateSub(sizeNow, ctx.builder.getInt32(1), "lastIdx");
        llvm::Value* slot = getElementPtr(ctx, listPtr, idx, elemType);
        llvm::Type* p = llvm::PointerType::get(ctx.context, 0);
        llvm::Function* dc = ctx.getFunction("aiml_ser_deep_copy", ctx.builder.getVoidTy(),
            {p, p, p, ctx.builder.getInt32Ty()});
        ctx.builder.CreateCall(dc, { slot, srcAddr, serNodes, ctx.builder.getInt32(serCount) });
    }

    llvm::Value* pop(CompilerContext& ctx, llvm::Value* listPtr, llvm::Type* elemType) {
        llvm::StructType* structType = getListStructType(ctx);

        llvm::Value* sizePtr = ctx.builder.CreateStructGEP(structType, listPtr, 0, "sizePtr");
        llvm::Value* sizeVal = ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), sizePtr, "size");
        
        llvm::Value* newSize = ctx.builder.CreateSub(sizeVal, ctx.builder.getInt32(1));
        ctx.builder.CreateStore(newSize, sizePtr);
        
        llvm::Value* dataPtrMember = ctx.builder.CreateStructGEP(structType, listPtr, 2, "dataPtr");
        llvm::Value* dataBase = ctx.builder.CreateLoad(llvm::PointerType::get(ctx.context, 0), dataPtrMember, "dataBase");
        
        llvm::Value* slotPtr = ctx.builder.CreateInBoundsGEP(elemType, dataBase, newSize);
        return ctx.builder.CreateLoad(elemType, slotPtr, "poppedVal");
    }

    llvm::Value* get(CompilerContext& ctx, llvm::Value* listPtr, llvm::Value* indexVal, llvm::Type* elemType) {
        llvm::StructType* structType = getListStructType(ctx);
        
        llvm::Value* dataPtrMember = ctx.builder.CreateStructGEP(structType, listPtr, 2, "dataPtr");
        llvm::Value* dataBase = ctx.builder.CreateLoad(llvm::PointerType::get(ctx.context, 0), dataPtrMember, "dataBase");
        
        llvm::Value* slotPtr = ctx.builder.CreateInBoundsGEP(elemType, dataBase, indexVal);
        return ctx.builder.CreateLoad(elemType, slotPtr, "getVal");
    }

    llvm::Value* getElementPtr(CompilerContext& ctx, llvm::Value* listPtr, llvm::Value* indexVal, llvm::Type* elemType) {
         llvm::StructType* structType = getListStructType(ctx);
         
         llvm::Value* dataPtrMember = ctx.builder.CreateStructGEP(structType, listPtr, 2, "dataPtr");
         llvm::Value* dataBase = ctx.builder.CreateLoad(llvm::PointerType::get(ctx.context, 0), dataPtrMember, "dataBase");
         
         return ctx.builder.CreateInBoundsGEP(elemType, dataBase, indexVal);
    }
    
    llvm::Value* slice(CompilerContext& ctx, llvm::Value* listPtr, llvm::Value* startVal, llvm::Value* endVal, llvm::Type* elemType) {
        // We need struct type for allocation of the new list
        llvm::StructType* structType = getListStructType(ctx);
        
        // Allocate struct on stack
        llvm::AllocaInst* newList = ctx.createEntryAlloca(structType, "slice_list");
        
        // Init
        initList(ctx, newList, elemType, 4); 
        
        llvm::Function* parentFunc = ctx.builder.GetInsertBlock()->getParent();
        llvm::BasicBlock* headerBB = llvm::BasicBlock::Create(ctx.context, "slice_loop_head", parentFunc);
        llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(ctx.context, "slice_loop_body", parentFunc);
        llvm::BasicBlock* exitBB = llvm::BasicBlock::Create(ctx.context, "slice_loop_exit", parentFunc);
        
        llvm::Value* iVar = ctx.createEntryAlloca(ctx.builder.getInt32Ty(), "i_slice");
        ctx.builder.CreateStore(startVal, iVar);
        
        ctx.builder.CreateBr(headerBB);
        
        // Header
        ctx.builder.SetInsertPoint(headerBB);
        llvm::Value* iVal = ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), iVar);
        llvm::Value* cond = ctx.builder.CreateICmpSLT(iVal, endVal);
        ctx.builder.CreateCondBr(cond, bodyBB, exitBB);
        
        // Body
        ctx.builder.SetInsertPoint(bodyBB);
        llvm::Value* elem = get(ctx, listPtr, iVal, elemType);
        push(ctx, newList, elem);
        
        llvm::Value* nextI = ctx.builder.CreateAdd(iVal, ctx.builder.getInt32(1));
        ctx.builder.CreateStore(nextI, iVar);
        ctx.builder.CreateBr(headerBB);
        
        // Exit
        ctx.builder.SetInsertPoint(exitBB);
        
        return newList;
    }

    llvm::Value* getSize(CompilerContext& ctx, llvm::Value* listPtr) {
        llvm::StructType* structType = getListStructType(ctx);
        llvm::Value* sizePtr = ctx.builder.CreateStructGEP(structType, listPtr, 0, "sizePtr");
        return ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), sizePtr, "sizeVal");
    }

    llvm::Value* isEmpty(CompilerContext& ctx, llvm::Value* listPtr) {
        llvm::Value* size = getSize(ctx, listPtr);
        return ctx.builder.CreateICmpEQ(size, ctx.builder.getInt32(0), "isEmpty");
    }

    void clear(CompilerContext& ctx, llvm::Value* listPtr) {
        llvm::StructType* structType = getListStructType(ctx);
        llvm::Value* sizePtr = ctx.builder.CreateStructGEP(structType, listPtr, 0, "sizePtr");
        ctx.builder.CreateStore(ctx.builder.getInt32(0), sizePtr);
    }

    void insert(CompilerContext& ctx, llvm::Value* listPtr, llvm::Value* indexVal, llvm::Value* val) {
        llvm::StructType* structType = getListStructType(ctx);
        llvm::Value* sizePtr = ctx.builder.CreateStructGEP(structType, listPtr, 0, "sizePtr");
        llvm::Value* capPtr = ctx.builder.CreateStructGEP(structType, listPtr, 1, "capPtr");
        
        llvm::Value* sizeVal = ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), sizePtr);
        llvm::Value* capVal = ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), capPtr);
        
        llvm::Function* parentFunc = ctx.builder.GetInsertBlock()->getParent();
        llvm::BasicBlock* growBB = llvm::BasicBlock::Create(ctx.context, "insert_grow", parentFunc);
        llvm::BasicBlock* moveBB = llvm::BasicBlock::Create(ctx.context, "insert_move", parentFunc);
        
        llvm::Value* isFull = ctx.builder.CreateICmpEQ(sizeVal, capVal);
        ctx.builder.CreateCondBr(isFull, growBB, moveBB);
        
        // Grow Block
        ctx.builder.SetInsertPoint(growBB);
        {
             llvm::Value* newCap = ctx.builder.CreateMul(capVal, ctx.builder.getInt32(2));
             ctx.builder.CreateStore(newCap, capPtr);
             
             llvm::Value* dataPtrMember = ctx.builder.CreateStructGEP(structType, listPtr, 2, "dataPtr");
             llvm::Value* oldData = ctx.builder.CreateLoad(llvm::PointerType::get(ctx.context, 0), dataPtrMember);
             
             llvm::Type* sz = ctx.getSizeType();
             llvm::Type* elemType = val->getType();
             llvm::Value* onePtr = ctx.builder.CreateConstInBoundsGEP1_32(elemType, llvm::Constant::getNullValue(elemType->getPointerTo()), 1);
             llvm::Value* elemSize = ctx.builder.CreatePtrToInt(onePtr, sz);
             llvm::Value* totalNewSize = ctx.builder.CreateMul(elemSize, ctx.builder.CreateZExtOrTrunc(newCap, sz));

             llvm::Function* reallocFunc = ctx.getFunction("realloc", llvm::PointerType::get(ctx.context, 0),
                {llvm::PointerType::get(ctx.context, 0), sz});
                
             llvm::Value* newData = ctx.builder.CreateCall(reallocFunc, {oldData, totalNewSize});
             ctx.builder.CreateStore(newData, dataPtrMember);
             
             ctx.builder.CreateBr(moveBB);
        }
        
        // Move Block
        ctx.builder.SetInsertPoint(moveBB);
        
        llvm::Value* dataPtrMember = ctx.builder.CreateStructGEP(structType, listPtr, 2, "dataPtr");
        llvm::Value* dataBase = ctx.builder.CreateLoad(llvm::PointerType::get(ctx.context, 0), dataPtrMember, "dataBase");
        
        llvm::Type* elemType = val->getType();
        
        llvm::Value* srcPtr = ctx.builder.CreateInBoundsGEP(elemType, dataBase, indexVal);
        llvm::Value* destPtr = ctx.builder.CreateInBoundsGEP(elemType, dataBase, ctx.builder.CreateAdd(indexVal, ctx.builder.getInt32(1)));
        
        llvm::Value* numToMove = ctx.builder.CreateSub(sizeVal, indexVal);
        
        // Count in bytes
        llvm::Type* sz = ctx.getSizeType();
        llvm::Value* onePtr = ctx.builder.CreateConstInBoundsGEP1_32(elemType, llvm::Constant::getNullValue(elemType->getPointerTo()), 1);
        llvm::Value* elemSize = ctx.builder.CreatePtrToInt(onePtr, sz);
        llvm::Value* bytesToMove = ctx.builder.CreateMul(ctx.builder.CreateZExtOrTrunc(numToMove, sz), elemSize);

        llvm::Function* memmoveFunc = ctx.getFunction("memmove", llvm::PointerType::get(ctx.context, 0),
             {llvm::PointerType::get(ctx.context, 0), llvm::PointerType::get(ctx.context, 0), sz});

        // Only move if numToMove > 0
        llvm::Value* shouldMove = ctx.builder.CreateICmpSGT(numToMove, ctx.builder.getInt32(0));

        llvm::BasicBlock* doMoveBB = llvm::BasicBlock::Create(ctx.context, "do_move", parentFunc);
        llvm::BasicBlock* storeBB = llvm::BasicBlock::Create(ctx.context, "insert_store", parentFunc);
        
        ctx.builder.CreateCondBr(shouldMove, doMoveBB, storeBB);
        
        ctx.builder.SetInsertPoint(doMoveBB);
        ctx.builder.CreateCall(memmoveFunc, {destPtr, srcPtr, bytesToMove});
        ctx.builder.CreateBr(storeBB);
        
        ctx.builder.SetInsertPoint(storeBB);
        ctx.builder.CreateStore(val, srcPtr);
        
        // size++
        llvm::Value* newSize = ctx.builder.CreateAdd(sizeVal, ctx.builder.getInt32(1));
        ctx.builder.CreateStore(newSize, sizePtr);
    }

    void remove(CompilerContext& ctx, llvm::Value* listPtr, llvm::Value* indexVal, llvm::Type* elemType, bool elemIsString) {
        llvm::StructType* structType = getListStructType(ctx);
        llvm::Value* sizePtr = ctx.builder.CreateStructGEP(structType, listPtr, 0, "sizePtr");
        llvm::Value* sizeVal = ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), sizePtr);

        llvm::Value* dataPtrMember = ctx.builder.CreateStructGEP(structType, listPtr, 2, "dataPtr");
        llvm::Value* dataBase = ctx.builder.CreateLoad(llvm::PointerType::get(ctx.context, 0), dataPtrMember, "dataBase");

        llvm::Value* destPtr = ctx.builder.CreateInBoundsGEP(elemType, dataBase, indexVal);
        // spec/61 §8: the list OWNS its string elements (push dups) → decref the removed element
        // before the memmove overwrites its slot, otherwise a churning capped list (event_log's
        // 40-line ring, effect/trigger logs) leaks one string per remove.
        if (elemIsString) {
            llvm::Type* i8p = llvm::PointerType::get(ctx.context, 0);
            llvm::Value* gone = ctx.builder.CreateLoad(i8p, destPtr, "rm_elem");
            llvm::Function* dec = ctx.getFunction("aiml_str_decref", ctx.builder.getVoidTy(), {i8p});
            ctx.builder.CreateCall(dec, { gone });
        }
        llvm::Value* srcPtr = ctx.builder.CreateInBoundsGEP(elemType, dataBase, ctx.builder.CreateAdd(indexVal, ctx.builder.getInt32(1)));
        
        llvm::Value* newSize = ctx.builder.CreateSub(sizeVal, ctx.builder.getInt32(1));
        llvm::Value* numToMove = ctx.builder.CreateSub(newSize, indexVal);
        
        llvm::Type* sz2 = ctx.getSizeType();
        llvm::Value* onePtr = ctx.builder.CreateConstInBoundsGEP1_32(elemType, llvm::Constant::getNullValue(elemType->getPointerTo()), 1);
        llvm::Value* elemSize = ctx.builder.CreatePtrToInt(onePtr, sz2);
        llvm::Value* bytesToMove = ctx.builder.CreateMul(ctx.builder.CreateZExtOrTrunc(numToMove, sz2), elemSize);

        llvm::Function* memmoveFunc = ctx.getFunction("memmove", llvm::PointerType::get(ctx.context, 0),
             {llvm::PointerType::get(ctx.context, 0), llvm::PointerType::get(ctx.context, 0), sz2});

        // Only move if numToMove > 0
        llvm::Value* shouldMove = ctx.builder.CreateICmpSGT(numToMove, ctx.builder.getInt32(0));

        llvm::Function* parentFunc = ctx.builder.GetInsertBlock()->getParent();
        llvm::BasicBlock* doMoveBB = llvm::BasicBlock::Create(ctx.context, "rm_do_move", parentFunc);
        llvm::BasicBlock* finishBB = llvm::BasicBlock::Create(ctx.context, "rm_finish", parentFunc);
        
        ctx.builder.CreateCondBr(shouldMove, doMoveBB, finishBB);
        
        ctx.builder.SetInsertPoint(doMoveBB);
        ctx.builder.CreateCall(memmoveFunc, {destPtr, srcPtr, bytesToMove});
        ctx.builder.CreateBr(finishBB);
        
        ctx.builder.SetInsertPoint(finishBB);
        ctx.builder.CreateStore(newSize, sizePtr);
    }

    // ── Shared IR helpers for the list-builtins (M1/M2) ─────────────────────
    namespace {
        llvm::PointerType* ptrTy(CompilerContext& ctx) {
            return llvm::PointerType::get(ctx.context, 0);
        }
        // Pointer to element slot `idx` (data + idx*sizeof(elem)).
        llvm::Value* slotPtr(CompilerContext& ctx, llvm::Value* listPtr,
                             llvm::Value* idx, llvm::Type* elemType) {
            return getElementPtr(ctx, listPtr, idx, elemType);
        }
        // Null-safe string equality (mirrors Compiler::visitStringCompare): equal
        // pointers or strcmp==0; a null on either side falls back to pointer eq.
        llvm::Value* emitStrEq(CompilerContext& ctx, llvm::Value* a, llvm::Value* b) {
            llvm::Function* pf = ctx.builder.GetInsertBlock()->getParent();
            auto* nullBB  = llvm::BasicBlock::Create(ctx.context, "leq_null", pf);
            auto* cmpBB   = llvm::BasicBlock::Create(ctx.context, "leq_cmp", pf);
            auto* mergeBB = llvm::BasicBlock::Create(ctx.context, "leq_merge", pf);
            llvm::Value* ptrEq   = ctx.builder.CreateICmpEQ(a, b);
            llvm::Value* anyNull = ctx.builder.CreateOr(ctx.builder.CreateIsNull(a),
                                                        ctx.builder.CreateIsNull(b));
            ctx.builder.CreateCondBr(anyNull, nullBB, cmpBB);
            ctx.builder.SetInsertPoint(nullBB);
            llvm::BasicBlock* nullEnd = ctx.builder.GetInsertBlock();
            ctx.builder.CreateBr(mergeBB);
            ctx.builder.SetInsertPoint(cmpBB);
            llvm::Function* strcmpFn = ctx.getFunction("strcmp", ctx.builder.getInt32Ty(),
                                                       {ptrTy(ctx), ptrTy(ctx)});
            llvm::Value* r = ctx.builder.CreateCall(strcmpFn, {a, b});
            llvm::Value* strEq = ctx.builder.CreateICmpEQ(r, ctx.builder.getInt32(0));
            llvm::BasicBlock* cmpEnd = ctx.builder.GetInsertBlock();
            ctx.builder.CreateBr(mergeBB);
            ctx.builder.SetInsertPoint(mergeBB);
            llvm::PHINode* phi = ctx.builder.CreatePHI(ctx.builder.getInt1Ty(), 2, "leq");
            phi->addIncoming(ptrEq, nullEnd);
            phi->addIncoming(strEq, cmpEnd);
            return phi;
        }
        // Element equality (i1).  string → content; float → oeq; everything else
        // (int/enum/bool/ptr) → bitwise/identity eq.
        llvm::Value* emitElemEq(CompilerContext& ctx, llvm::Value* a, llvm::Value* b,
                                llvm::Type* elemType, const std::string& elemTypeName) {
            if (elemTypeName == "string") return emitStrEq(ctx, a, b);
            if (elemType->isFloatingPointTy()) return ctx.builder.CreateFCmpOEQ(a, b);
            return ctx.builder.CreateICmpEQ(a, b);
        }
        bool isUnsignedElem(const std::string& t) {
            return t == "u8" || t == "u16" || t == "u32" || t == "u64" || t == "bool";
        }
        // Null-safe "a < b" for sort.  string: null treated as "" (empty). float:
        // olt (NaN never < x → sinks toward the end under insertion sort).
        llvm::Value* emitElemLt(CompilerContext& ctx, llvm::Value* a, llvm::Value* b,
                                llvm::Type* elemType, const std::string& elemTypeName) {
            if (elemTypeName == "string") {
                llvm::Value* empty = ctx.builder.CreateGlobalStringPtr("", "lstr_empty");
                llvm::Value* aa = ctx.builder.CreateSelect(ctx.builder.CreateIsNull(a), empty, a);
                llvm::Value* bb = ctx.builder.CreateSelect(ctx.builder.CreateIsNull(b), empty, b);
                llvm::Function* strcmpFn = ctx.getFunction("strcmp", ctx.builder.getInt32Ty(),
                                                           {ptrTy(ctx), ptrTy(ctx)});
                llvm::Value* r = ctx.builder.CreateCall(strcmpFn, {aa, bb});
                return ctx.builder.CreateICmpSLT(r, ctx.builder.getInt32(0));
            }
            if (elemType->isFloatingPointTy()) return ctx.builder.CreateFCmpOLT(a, b);
            if (isUnsignedElem(elemTypeName)) return ctx.builder.CreateICmpULT(a, b);
            return ctx.builder.CreateICmpSLT(a, b);
        }
    } // anonymous namespace

    llvm::Value* indexOf(CompilerContext& ctx, llvm::Value* listPtr, llvm::Value* needle,
                         llvm::Type* elemType, const std::string& elemTypeName) {
        llvm::Function* pf = ctx.builder.GetInsertBlock()->getParent();
        llvm::Value* resVar = ctx.createEntryAlloca(ctx.builder.getInt32Ty(), "lidx_res");
        llvm::Value* iVar   = ctx.createEntryAlloca(ctx.builder.getInt32Ty(), "lidx_i");
        ctx.builder.CreateStore(ctx.builder.getInt32(-1), resVar);
        ctx.builder.CreateStore(ctx.builder.getInt32(0), iVar);
        llvm::Value* size = getSize(ctx, listPtr);
        auto* head = llvm::BasicBlock::Create(ctx.context, "lidx_head", pf);
        auto* body = llvm::BasicBlock::Create(ctx.context, "lidx_body", pf);
        auto* hit  = llvm::BasicBlock::Create(ctx.context, "lidx_hit", pf);
        auto* next = llvm::BasicBlock::Create(ctx.context, "lidx_next", pf);
        auto* exit = llvm::BasicBlock::Create(ctx.context, "lidx_exit", pf);
        ctx.builder.CreateBr(head);
        ctx.builder.SetInsertPoint(head);
        llvm::Value* iVal   = ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), iVar, "i");
        llvm::Value* resVal = ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), resVar, "res");
        llvm::Value* more = ctx.builder.CreateAnd(
            ctx.builder.CreateICmpSLT(iVal, size),
            ctx.builder.CreateICmpEQ(resVal, ctx.builder.getInt32(-1)));
        ctx.builder.CreateCondBr(more, body, exit);
        ctx.builder.SetInsertPoint(body);
        llvm::Value* elem = ctx.builder.CreateLoad(elemType, slotPtr(ctx, listPtr, iVal, elemType), "elem");
        ctx.builder.CreateCondBr(emitElemEq(ctx, elem, needle, elemType, elemTypeName), hit, next);
        ctx.builder.SetInsertPoint(hit);
        ctx.builder.CreateStore(iVal, resVar);
        ctx.builder.CreateBr(next);
        ctx.builder.SetInsertPoint(next);
        ctx.builder.CreateStore(ctx.builder.CreateAdd(iVal, ctx.builder.getInt32(1)), iVar);
        ctx.builder.CreateBr(head);
        ctx.builder.SetInsertPoint(exit);
        return ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), resVar, "lidx_result");
    }

    llvm::Value* count(CompilerContext& ctx, llvm::Value* listPtr, llvm::Value* needle,
                       llvm::Type* elemType, const std::string& elemTypeName) {
        llvm::Function* pf = ctx.builder.GetInsertBlock()->getParent();
        llvm::Value* cntVar = ctx.createEntryAlloca(ctx.builder.getInt32Ty(), "lcnt_c");
        llvm::Value* iVar   = ctx.createEntryAlloca(ctx.builder.getInt32Ty(), "lcnt_i");
        ctx.builder.CreateStore(ctx.builder.getInt32(0), cntVar);
        ctx.builder.CreateStore(ctx.builder.getInt32(0), iVar);
        llvm::Value* size = getSize(ctx, listPtr);
        auto* head = llvm::BasicBlock::Create(ctx.context, "lcnt_head", pf);
        auto* body = llvm::BasicBlock::Create(ctx.context, "lcnt_body", pf);
        auto* inc  = llvm::BasicBlock::Create(ctx.context, "lcnt_inc", pf);
        auto* next = llvm::BasicBlock::Create(ctx.context, "lcnt_next", pf);
        auto* exit = llvm::BasicBlock::Create(ctx.context, "lcnt_exit", pf);
        ctx.builder.CreateBr(head);
        ctx.builder.SetInsertPoint(head);
        llvm::Value* iVal = ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), iVar, "i");
        ctx.builder.CreateCondBr(ctx.builder.CreateICmpSLT(iVal, size), body, exit);
        ctx.builder.SetInsertPoint(body);
        llvm::Value* elem = ctx.builder.CreateLoad(elemType, slotPtr(ctx, listPtr, iVal, elemType), "elem");
        ctx.builder.CreateCondBr(emitElemEq(ctx, elem, needle, elemType, elemTypeName), inc, next);
        ctx.builder.SetInsertPoint(inc);
        llvm::Value* c = ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), cntVar, "c");
        ctx.builder.CreateStore(ctx.builder.CreateAdd(c, ctx.builder.getInt32(1)), cntVar);
        ctx.builder.CreateBr(next);
        ctx.builder.SetInsertPoint(next);
        ctx.builder.CreateStore(ctx.builder.CreateAdd(iVal, ctx.builder.getInt32(1)), iVar);
        ctx.builder.CreateBr(head);
        ctx.builder.SetInsertPoint(exit);
        return ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), cntVar, "lcnt_result");
    }

    void extend(CompilerContext& ctx, llvm::Value* dstPtr, llvm::Value* srcPtr, llvm::Type* elemType) {
        llvm::Function* pf = ctx.builder.GetInsertBlock()->getParent();
        // Snapshot src length BEFORE any push: self.extend(self) must append only
        // the ORIGINAL elements, and push may realloc.  get()/push re-load the
        // data pointer each call, so a realloc mid-loop is safe.
        llvm::Value* srcLen = getSize(ctx, srcPtr);
        llvm::Value* iVar = ctx.createEntryAlloca(ctx.builder.getInt32Ty(), "lext_i");
        ctx.builder.CreateStore(ctx.builder.getInt32(0), iVar);
        auto* head = llvm::BasicBlock::Create(ctx.context, "lext_head", pf);
        auto* body = llvm::BasicBlock::Create(ctx.context, "lext_body", pf);
        auto* exit = llvm::BasicBlock::Create(ctx.context, "lext_exit", pf);
        ctx.builder.CreateBr(head);
        ctx.builder.SetInsertPoint(head);
        llvm::Value* iVal = ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), iVar, "i");
        ctx.builder.CreateCondBr(ctx.builder.CreateICmpSLT(iVal, srcLen), body, exit);
        ctx.builder.SetInsertPoint(body);
        llvm::Value* elem = get(ctx, srcPtr, iVal, elemType);   // by-value copy
        push(ctx, dstPtr, elem);
        ctx.builder.CreateStore(ctx.builder.CreateAdd(iVal, ctx.builder.getInt32(1)), iVar);
        ctx.builder.CreateBr(head);
        ctx.builder.SetInsertPoint(exit);
    }

    void reverse(CompilerContext& ctx, llvm::Value* listPtr, llvm::Type* elemType) {
        llvm::Function* pf = ctx.builder.GetInsertBlock()->getParent();
        llvm::Value* size = getSize(ctx, listPtr);
        llvm::Value* iVar = ctx.createEntryAlloca(ctx.builder.getInt32Ty(), "lrev_i");
        llvm::Value* jVar = ctx.createEntryAlloca(ctx.builder.getInt32Ty(), "lrev_j");
        ctx.builder.CreateStore(ctx.builder.getInt32(0), iVar);
        ctx.builder.CreateStore(ctx.builder.CreateSub(size, ctx.builder.getInt32(1)), jVar);
        auto* head = llvm::BasicBlock::Create(ctx.context, "lrev_head", pf);
        auto* body = llvm::BasicBlock::Create(ctx.context, "lrev_body", pf);
        auto* exit = llvm::BasicBlock::Create(ctx.context, "lrev_exit", pf);
        ctx.builder.CreateBr(head);
        ctx.builder.SetInsertPoint(head);
        llvm::Value* iVal = ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), iVar, "i");
        llvm::Value* jVal = ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), jVar, "j");
        ctx.builder.CreateCondBr(ctx.builder.CreateICmpSLT(iVal, jVal), body, exit);
        ctx.builder.SetInsertPoint(body);
        llvm::Value* pi = slotPtr(ctx, listPtr, iVal, elemType);
        llvm::Value* pj = slotPtr(ctx, listPtr, jVal, elemType);
        llvm::Value* vi = ctx.builder.CreateLoad(elemType, pi, "vi");
        llvm::Value* vj = ctx.builder.CreateLoad(elemType, pj, "vj");
        ctx.builder.CreateStore(vj, pi);
        ctx.builder.CreateStore(vi, pj);
        ctx.builder.CreateStore(ctx.builder.CreateAdd(iVal, ctx.builder.getInt32(1)), iVar);
        ctx.builder.CreateStore(ctx.builder.CreateSub(jVal, ctx.builder.getInt32(1)), jVar);
        ctx.builder.CreateBr(head);
        ctx.builder.SetInsertPoint(exit);
    }

    // Stable insertion sort in IR.  ascending: shift while data[j] > key
    // (key < data[j]); descending: shift while data[j] < key.
    void sortList(CompilerContext& ctx, llvm::Value* listPtr, llvm::Type* elemType,
                  const std::string& elemTypeName, bool descending) {
        llvm::Function* pf = ctx.builder.GetInsertBlock()->getParent();
        llvm::Value* size = getSize(ctx, listPtr);
        llvm::Value* iVar = ctx.createEntryAlloca(ctx.builder.getInt32Ty(), "lsrt_i");
        llvm::Value* jVar = ctx.createEntryAlloca(ctx.builder.getInt32Ty(), "lsrt_j");
        llvm::Value* keyVar = ctx.createEntryAlloca(elemType, "lsrt_key");
        ctx.builder.CreateStore(ctx.builder.getInt32(1), iVar);
        auto* oHead = llvm::BasicBlock::Create(ctx.context, "lsrt_ohead", pf);
        auto* oBody = llvm::BasicBlock::Create(ctx.context, "lsrt_obody", pf);
        auto* iHead = llvm::BasicBlock::Create(ctx.context, "lsrt_ihead", pf);
        auto* iCmp  = llvm::BasicBlock::Create(ctx.context, "lsrt_icmp", pf);
        auto* iBody = llvm::BasicBlock::Create(ctx.context, "lsrt_ibody", pf);
        auto* iDone = llvm::BasicBlock::Create(ctx.context, "lsrt_idone", pf);
        auto* exit  = llvm::BasicBlock::Create(ctx.context, "lsrt_exit", pf);
        ctx.builder.CreateBr(oHead);
        // outer: for i = 1; i < size; i++
        ctx.builder.SetInsertPoint(oHead);
        llvm::Value* iVal = ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), iVar, "i");
        ctx.builder.CreateCondBr(ctx.builder.CreateICmpSLT(iVal, size), oBody, exit);
        ctx.builder.SetInsertPoint(oBody);
        llvm::Value* key = ctx.builder.CreateLoad(elemType, slotPtr(ctx, listPtr, iVal, elemType), "key");
        ctx.builder.CreateStore(key, keyVar);
        ctx.builder.CreateStore(ctx.builder.CreateSub(iVal, ctx.builder.getInt32(1)), jVar);
        ctx.builder.CreateBr(iHead);
        // inner head: check j >= 0 FIRST (no OOB element load when j == -1).
        ctx.builder.SetInsertPoint(iHead);
        llvm::Value* jVal = ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), jVar, "j");
        ctx.builder.CreateCondBr(ctx.builder.CreateICmpSGE(jVal, ctx.builder.getInt32(0)), iCmp, iDone);
        // inner cmp: j >= 0 guaranteed → safe to load data[j] and compare.
        ctx.builder.SetInsertPoint(iCmp);
        llvm::Value* keyV = ctx.builder.CreateLoad(elemType, keyVar, "keyv");
        llvm::Value* atJ  = ctx.builder.CreateLoad(elemType, slotPtr(ctx, listPtr, jVal, elemType), "atj");
        llvm::Value* shift = descending
            ? emitElemLt(ctx, atJ, keyV, elemType, elemTypeName)   // data[j] < key
            : emitElemLt(ctx, keyV, atJ, elemType, elemTypeName);  // key < data[j]
        ctx.builder.CreateCondBr(shift, iBody, iDone);
        ctx.builder.SetInsertPoint(iBody);
        // data[j+1] = data[j]
        llvm::Value* jp1 = ctx.builder.CreateAdd(jVal, ctx.builder.getInt32(1));
        ctx.builder.CreateStore(atJ, slotPtr(ctx, listPtr, jp1, elemType));
        ctx.builder.CreateStore(ctx.builder.CreateSub(jVal, ctx.builder.getInt32(1)), jVar);
        ctx.builder.CreateBr(iHead);
        ctx.builder.SetInsertPoint(iDone);
        // data[j+1] = key
        llvm::Value* jDone = ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), jVar, "jd");
        llvm::Value* keyF  = ctx.builder.CreateLoad(elemType, keyVar, "keyf");
        ctx.builder.CreateStore(keyF, slotPtr(ctx, listPtr,
            ctx.builder.CreateAdd(jDone, ctx.builder.getInt32(1)), elemType));
        ctx.builder.CreateStore(ctx.builder.CreateAdd(iVal, ctx.builder.getInt32(1)), iVar);
        ctx.builder.CreateBr(oHead);
        ctx.builder.SetInsertPoint(exit);
    }

    void shuffle(CompilerContext& ctx, llvm::Value* listPtr, llvm::Type* elemType) {
        llvm::Function* pf = ctx.builder.GetInsertBlock()->getParent();
        llvm::Value* size = getSize(ctx, listPtr);
        llvm::Function* rndFn = ctx.getFunction("aiml_math_random_int", ctx.builder.getInt32Ty(),
                                                {ctx.builder.getInt32Ty(), ctx.builder.getInt32Ty()});
        llvm::Value* iVar = ctx.createEntryAlloca(ctx.builder.getInt32Ty(), "lshf_i");
        // Fisher-Yates: for i = size-1; i >= 1; i-- : j = rand[0,i]; swap(i,j)
        ctx.builder.CreateStore(ctx.builder.CreateSub(size, ctx.builder.getInt32(1)), iVar);
        auto* head = llvm::BasicBlock::Create(ctx.context, "lshf_head", pf);
        auto* body = llvm::BasicBlock::Create(ctx.context, "lshf_body", pf);
        auto* exit = llvm::BasicBlock::Create(ctx.context, "lshf_exit", pf);
        ctx.builder.CreateBr(head);
        ctx.builder.SetInsertPoint(head);
        llvm::Value* iVal = ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), iVar, "i");
        ctx.builder.CreateCondBr(ctx.builder.CreateICmpSGE(iVal, ctx.builder.getInt32(1)), body, exit);
        ctx.builder.SetInsertPoint(body);
        // j = random_int(0, i+1)  → inclusive [0, i]
        llvm::Value* hi = ctx.builder.CreateAdd(iVal, ctx.builder.getInt32(1));
        llvm::Value* jVal = ctx.builder.CreateCall(rndFn, {ctx.builder.getInt32(0), hi}, "j");
        llvm::Value* pi = slotPtr(ctx, listPtr, iVal, elemType);
        llvm::Value* pj = slotPtr(ctx, listPtr, jVal, elemType);
        llvm::Value* vi = ctx.builder.CreateLoad(elemType, pi, "vi");
        llvm::Value* vj = ctx.builder.CreateLoad(elemType, pj, "vj");
        ctx.builder.CreateStore(vj, pi);
        ctx.builder.CreateStore(vi, pj);
        ctx.builder.CreateStore(ctx.builder.CreateSub(iVal, ctx.builder.getInt32(1)), iVar);
        ctx.builder.CreateBr(head);
        ctx.builder.SetInsertPoint(exit);
    }


}
