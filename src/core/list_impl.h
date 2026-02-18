#pragma once
#include "context.h"
#include <llvm/IR/IRBuilder.h>

namespace ListImpl {

    // Helper: Reconstruct Struct Type from Elem type
    // We assume the type name is "list_<ElemTypeName>" but we might not have the name string here easily if elemType is generic.
    // However, we know the fields: {i32, i32, ptr} for the opaque pointer world, actually {i32, i32, ptr} is sufficient?
    // Wait, StructTy is needed for GEP.
    // If we can't find it by name, we can create a literal struct type on the fly or look it up.
    // Since we created it in Context with a name, we should try to find it.
    // But `elemType` doesn't give us the name of the list struct directly unless we map it.
    
    // BETTER APPROACH: Use a fixed structure layout compatible with all lists for the implementation internal ops.
    // { i32 size, i32 capacity, void* data }
    // In strict LLVM, type names matter for `getTypeByName`.
    // But for `CreateStructGEP`, we just need *A* struct type that matches the layout.
    // Let's define a helper to get/create the layout.
    
    inline llvm::StructType* getListStructType(CompilerContext& ctx) {
        // Return generic { i32, i32, ptr }
        // We can reuse this for all lists if we don't strictly use named types for GEP.
        // But `listPtr` passed in points to a specific named struct "list_i32" etc.
        // Opaque pointers: The pointer is just `ptr`.
        // The GEP instruction needs a Source Element Type.
        // If we use a generic `{i32, i32, ptr}` struct type, GEP will work fine as long as offsets match.
        static llvm::StructType* genericListTy = nullptr;
        if (!genericListTy) {
             genericListTy = llvm::StructType::create(ctx.context, {
                 ctx.builder.getInt32Ty(), // size
                 ctx.builder.getInt32Ty(), // capacity
                 llvm::PointerType::get(ctx.context, 0) // data (opaque ptr)
             }, "GenericList"); // Anon or named
        }
        return genericListTy;
    }

    inline void initList(CompilerContext& ctx, llvm::Value* listPtr, llvm::Type* elemType, int initialCapacity = 4) {
        llvm::Type* i32 = ctx.builder.getInt32Ty();
        llvm::Type* i64 = ctx.builder.getInt64Ty();
        llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0); 
        
        llvm::StructType* structType = getListStructType(ctx);

        // 1. Set Size = 0
        llvm::Value* sizePtr = ctx.builder.CreateStructGEP(structType, listPtr, 0, "sizePtr");
        ctx.builder.CreateStore(ctx.builder.getInt32(0), sizePtr);
        
        // 2. Set Capacity = initialCapacity
        llvm::Value* capPtr = ctx.builder.CreateStructGEP(structType, listPtr, 1, "capPtr");
        ctx.builder.CreateStore(ctx.builder.getInt32(initialCapacity), capPtr);
        
        // 3. Malloc Data
        llvm::Value* onePtr = ctx.builder.CreateConstInBoundsGEP1_32(elemType, llvm::Constant::getNullValue(elemType->getPointerTo()), 1);
        llvm::Value* elemSize = ctx.builder.CreatePtrToInt(onePtr, i64);
        
        llvm::Value* sizeVal = ctx.builder.getInt32(initialCapacity);
        llvm::Value* totalSize = ctx.builder.CreateMul(elemSize, ctx.builder.CreateZExt(sizeVal, i64));
        
        llvm::Function* mallocFunc = ctx.getFunction("malloc", ptrTy, {i64});
        llvm::Value* dataVoidPtr = ctx.builder.CreateCall(mallocFunc, {totalSize});
        
        llvm::Value* dataPtrMember = ctx.builder.CreateStructGEP(structType, listPtr, 2, "dataPtr");
        ctx.builder.CreateStore(dataVoidPtr, dataPtrMember);
    }
    
    inline void push(CompilerContext& ctx, llvm::Value* listPtr, llvm::Value* val) {
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
            llvm::Value* newCap = ctx.builder.CreateMul(capVal, ctx.builder.getInt32(2));
            ctx.builder.CreateStore(newCap, capPtr);
            
            llvm::Value* dataPtrMember = ctx.builder.CreateStructGEP(structType, listPtr, 2, "dataPtr");
            llvm::Value* oldData = ctx.builder.CreateLoad(llvm::PointerType::get(ctx.context, 0), dataPtrMember);
            
            llvm::Type* elemType = val->getType();
            llvm::Value* onePtr = ctx.builder.CreateConstInBoundsGEP1_32(elemType, llvm::Constant::getNullValue(elemType->getPointerTo()), 1);
            llvm::Value* elemSize = ctx.builder.CreatePtrToInt(onePtr, ctx.builder.getInt64Ty());
            llvm::Value* totalNewSize = ctx.builder.CreateMul(elemSize, ctx.builder.CreateZExt(newCap, ctx.builder.getInt64Ty()));
            
            llvm::Function* reallocFunc = ctx.getFunction("realloc", llvm::PointerType::get(ctx.context, 0), 
                {llvm::PointerType::get(ctx.context, 0), ctx.builder.getInt64Ty()});
                
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

    inline llvm::Value* pop(CompilerContext& ctx, llvm::Value* listPtr, llvm::Type* elemType) {
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

    inline llvm::Value* get(CompilerContext& ctx, llvm::Value* listPtr, llvm::Value* indexVal, llvm::Type* elemType) {
        llvm::StructType* structType = getListStructType(ctx);
        
        llvm::Value* dataPtrMember = ctx.builder.CreateStructGEP(structType, listPtr, 2, "dataPtr");
        llvm::Value* dataBase = ctx.builder.CreateLoad(llvm::PointerType::get(ctx.context, 0), dataPtrMember, "dataBase");
        
        llvm::Value* slotPtr = ctx.builder.CreateInBoundsGEP(elemType, dataBase, indexVal);
        return ctx.builder.CreateLoad(elemType, slotPtr, "getVal");
    }

    inline llvm::Value* getElementPtr(CompilerContext& ctx, llvm::Value* listPtr, llvm::Value* indexVal, llvm::Type* elemType) {
         llvm::StructType* structType = getListStructType(ctx);
         
         llvm::Value* dataPtrMember = ctx.builder.CreateStructGEP(structType, listPtr, 2, "dataPtr");
         llvm::Value* dataBase = ctx.builder.CreateLoad(llvm::PointerType::get(ctx.context, 0), dataPtrMember, "dataBase");
         
         return ctx.builder.CreateInBoundsGEP(elemType, dataBase, indexVal);
    }
    
    inline llvm::Value* slice(CompilerContext& ctx, llvm::Value* listPtr, llvm::Value* startVal, llvm::Value* endVal, llvm::Type* elemType) {
        // We need struct type for allocation of the new list
        llvm::StructType* structType = getListStructType(ctx);
        
        // Allocate struct on stack
        llvm::AllocaInst* newList = ctx.builder.CreateAlloca(structType, nullptr, "slice_list");
        
        // Init
        initList(ctx, newList, elemType, 4); 
        
        llvm::Function* parentFunc = ctx.builder.GetInsertBlock()->getParent();
        llvm::BasicBlock* headerBB = llvm::BasicBlock::Create(ctx.context, "slice_loop_head", parentFunc);
        llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(ctx.context, "slice_loop_body", parentFunc);
        llvm::BasicBlock* exitBB = llvm::BasicBlock::Create(ctx.context, "slice_loop_exit", parentFunc);
        
        llvm::Value* iVar = ctx.builder.CreateAlloca(ctx.builder.getInt32Ty(), nullptr, "i_slice");
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

    inline llvm::Value* getSize(CompilerContext& ctx, llvm::Value* listPtr) {
        llvm::StructType* structType = getListStructType(ctx);
        llvm::Value* sizePtr = ctx.builder.CreateStructGEP(structType, listPtr, 0, "sizePtr");
        return ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), sizePtr, "sizeVal");
    }

    inline llvm::Value* isEmpty(CompilerContext& ctx, llvm::Value* listPtr) {
        llvm::Value* size = getSize(ctx, listPtr);
        return ctx.builder.CreateICmpEQ(size, ctx.builder.getInt32(0), "isEmpty");
    }

    inline void clear(CompilerContext& ctx, llvm::Value* listPtr) {
        llvm::StructType* structType = getListStructType(ctx);
        llvm::Value* sizePtr = ctx.builder.CreateStructGEP(structType, listPtr, 0, "sizePtr");
        ctx.builder.CreateStore(ctx.builder.getInt32(0), sizePtr);
    }

    inline void insert(CompilerContext& ctx, llvm::Value* listPtr, llvm::Value* indexVal, llvm::Value* val) {
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
             
             llvm::Type* elemType = val->getType();
             llvm::Value* onePtr = ctx.builder.CreateConstInBoundsGEP1_32(elemType, llvm::Constant::getNullValue(elemType->getPointerTo()), 1);
             llvm::Value* elemSize = ctx.builder.CreatePtrToInt(onePtr, ctx.builder.getInt64Ty());
             llvm::Value* totalNewSize = ctx.builder.CreateMul(elemSize, ctx.builder.CreateZExt(newCap, ctx.builder.getInt64Ty()));
             
             llvm::Function* reallocFunc = ctx.getFunction("realloc", llvm::PointerType::get(ctx.context, 0), 
                {llvm::PointerType::get(ctx.context, 0), ctx.builder.getInt64Ty()});
                
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
        llvm::Value* onePtr = ctx.builder.CreateConstInBoundsGEP1_32(elemType, llvm::Constant::getNullValue(elemType->getPointerTo()), 1);
        llvm::Value* elemSize = ctx.builder.CreatePtrToInt(onePtr, ctx.builder.getInt64Ty());
        llvm::Value* bytesToMove = ctx.builder.CreateMul(ctx.builder.CreateZExt(numToMove, ctx.builder.getInt64Ty()), elemSize);
        
        llvm::Function* memmoveFunc = ctx.getFunction("memmove", llvm::PointerType::get(ctx.context, 0), 
             {llvm::PointerType::get(ctx.context, 0), llvm::PointerType::get(ctx.context, 0), ctx.builder.getInt64Ty()});
        
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

    inline void remove(CompilerContext& ctx, llvm::Value* listPtr, llvm::Value* indexVal, llvm::Type* elemType) {
        llvm::StructType* structType = getListStructType(ctx);
        llvm::Value* sizePtr = ctx.builder.CreateStructGEP(structType, listPtr, 0, "sizePtr");
        llvm::Value* sizeVal = ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), sizePtr);
        
        llvm::Value* dataPtrMember = ctx.builder.CreateStructGEP(structType, listPtr, 2, "dataPtr");
        llvm::Value* dataBase = ctx.builder.CreateLoad(llvm::PointerType::get(ctx.context, 0), dataPtrMember, "dataBase");
        
        llvm::Value* destPtr = ctx.builder.CreateInBoundsGEP(elemType, dataBase, indexVal);
        llvm::Value* srcPtr = ctx.builder.CreateInBoundsGEP(elemType, dataBase, ctx.builder.CreateAdd(indexVal, ctx.builder.getInt32(1)));
        
        llvm::Value* newSize = ctx.builder.CreateSub(sizeVal, ctx.builder.getInt32(1));
        llvm::Value* numToMove = ctx.builder.CreateSub(newSize, indexVal);
        
        llvm::Value* onePtr = ctx.builder.CreateConstInBoundsGEP1_32(elemType, llvm::Constant::getNullValue(elemType->getPointerTo()), 1);
        llvm::Value* elemSize = ctx.builder.CreatePtrToInt(onePtr, ctx.builder.getInt64Ty());
        llvm::Value* bytesToMove = ctx.builder.CreateMul(ctx.builder.CreateZExt(numToMove, ctx.builder.getInt64Ty()), elemSize);
        
        llvm::Function* memmoveFunc = ctx.getFunction("memmove", llvm::PointerType::get(ctx.context, 0), 
             {llvm::PointerType::get(ctx.context, 0), llvm::PointerType::get(ctx.context, 0), ctx.builder.getInt64Ty()});

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

}
