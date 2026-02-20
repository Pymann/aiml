#include "map_impl.h"
#include <iostream>

namespace MapImpl {

    llvm::StructType* getMapStructType(CompilerContext& ctx, llvm::Type* keyType, llvm::Type* valType) {
        // Try to find named struct first
        // We need to know the name format used in context.h: "map_Key_Val"
        // But we don't have the string names of types here easily unless we pass them.
        // However, we can reconstruct the generic struct if needed.
        // BETTER: The caller should pass the StructType if known.
        // But for now, let's just make sure the structural layout is IDENTICAL.
        
        std::vector<llvm::Type*> members;
        members.push_back(ctx.builder.getInt32Ty()); // size
        members.push_back(ctx.builder.getInt32Ty()); // capacity
        members.push_back(ctx.builder.getInt8Ty()->getPointerTo()); // occupied (i8*)
        
        // Keys and Values are pointers to K and V
        // For opaque pointers, these are just ptr. 
        // In Context.h: members.push_back(llvm::PointerType::get(context, 0));
        members.push_back(llvm::PointerType::get(ctx.context, 0)); // keys
        members.push_back(llvm::PointerType::get(ctx.context, 0)); // values
        
        return llvm::StructType::get(ctx.context, members);
    }

    void initMap(CompilerContext& ctx, llvm::Value* mapPtr, llvm::Type* keyType, llvm::Type* valType, int initialCapacity) {
        llvm::StructType* structType = getMapStructType(ctx, keyType, valType);
        
        // Use the actual type of mapPtr if it is a pointer to a struct
        llvm::Type* mapPtrType = mapPtr->getType();
        if (mapPtrType->isPointerTy()) {
             // We can't easily get the element type of an opaque pointer.
             // But we can assume mapPtr points to the correct struct.
             // Use structType for GEP.
        }

        llvm::Type* i32 = ctx.builder.getInt32Ty();
        llvm::Type* i64 = ctx.builder.getInt64Ty();
        llvm::Type* i8 = ctx.builder.getInt8Ty();
        llvm::Type* ptrTy = llvm::PointerType::get(ctx.context, 0);

        // 1. Set Size = 0
        llvm::Value* sizePtr = ctx.builder.CreateStructGEP(structType, mapPtr, 0, "sizePtr");
        ctx.builder.CreateStore(ctx.builder.getInt32(0), sizePtr);

        // 2. Set Capacity
        llvm::Value* capPtr = ctx.builder.CreateStructGEP(structType, mapPtr, 1, "capPtr");
        ctx.builder.CreateStore(ctx.builder.getInt32(initialCapacity), capPtr);
        
        llvm::Value* capVal = ctx.builder.getInt32(initialCapacity);
        llvm::Value* capVal64 = ctx.builder.CreateZExt(capVal, i64);

        // 3. Alloc Occupied (i8 array)
        llvm::Function* callocFunc = ctx.getFunction("calloc", ptrTy, {i64, i64});
        llvm::Value* occupiedPtr = ctx.builder.CreateCall(callocFunc, {capVal64, ctx.builder.getInt64(1)}); 
        
        llvm::Value* occMember = ctx.builder.CreateStructGEP(structType, mapPtr, 2, "occPtr");
        ctx.builder.CreateStore(occupiedPtr, occMember);

        // 4. Alloc Keys
        llvm::Value* keyOne = ctx.builder.CreateConstInBoundsGEP1_32(keyType, llvm::Constant::getNullValue(keyType->getPointerTo()), 1);
        llvm::Value* keySize = ctx.builder.CreatePtrToInt(keyOne, i64);
        llvm::Function* mallocFunc = ctx.getFunction("malloc", ptrTy, {i64});
        
        llvm::Value* keysSizeTotal = ctx.builder.CreateMul(keySize, capVal64);
        llvm::Value* keysPtrRaw = ctx.builder.CreateCall(mallocFunc, {keysSizeTotal});
        // BitCast not strictly needed for opaque pointers but good for clarity/compat
        llvm::Value* keysPtr = keysPtrRaw; 
        
        llvm::Value* keysMember = ctx.builder.CreateStructGEP(structType, mapPtr, 3, "keysPtr");
        ctx.builder.CreateStore(keysPtr, keysMember);

        // 5. Alloc Values
        llvm::Value* valOne = ctx.builder.CreateConstInBoundsGEP1_32(valType, llvm::Constant::getNullValue(valType->getPointerTo()), 1);
        llvm::Value* valSize = ctx.builder.CreatePtrToInt(valOne, i64);
        
        llvm::Value* valsSizeTotal = ctx.builder.CreateMul(valSize, capVal64);
        llvm::Value* valsPtr = ctx.builder.CreateCall(mallocFunc, {valsSizeTotal});
        
        llvm::Value* dataPtrMember = ctx.builder.CreateStructGEP(structType, mapPtr, 4, "valsPtr");
        ctx.builder.CreateStore(valsPtr, dataPtrMember);
    }
    
    llvm::Value* compareKeys(CompilerContext& ctx, llvm::Value* k1, llvm::Value* k2, llvm::Type* keyType) {
        if (keyType->isIntegerTy()) {
            return ctx.builder.CreateICmpEQ(k1, k2, "key_eq");
        } else if (keyType->isPointerTy()) {
             // Assume string (i8*)
             // Use strcmp
             llvm::Type* i8Ptr = ctx.builder.getInt8Ty()->getPointerTo();
             llvm::Function* strcmpFunc = ctx.getFunction("strcmp", ctx.builder.getInt32Ty(), {i8Ptr, i8Ptr});
             llvm::Value* res = ctx.builder.CreateCall(strcmpFunc, {k1, k2});
             return ctx.builder.CreateICmpEQ(res, ctx.builder.getInt32(0), "str_eq");
        }
        return ctx.builder.getInt1(false);
    }

    llvm::Value* getSize(CompilerContext& ctx, llvm::Value* mapPtr, llvm::StructType* mapType) {
        llvm::Value* sizePtr = ctx.builder.CreateStructGEP(mapType, mapPtr, 0, "sizePtr");
        return ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), sizePtr, "size_val");
    }

    void put(CompilerContext& ctx, llvm::Value* mapPtr, llvm::Value* keyVal, llvm::Value* valVal, llvm::StructType* mapType) {
        // Implement Put (Linear Scan)
        llvm::Value* sizePtr = ctx.builder.CreateStructGEP(mapType, mapPtr, 0, "sizePtr");
        llvm::Value* capPtr = ctx.builder.CreateStructGEP(mapType, mapPtr, 1, "capPtr");
        
        llvm::Value* sizeVal = ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), sizePtr, "size");
        llvm::Value* capVal = ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), capPtr, "capacity");

        llvm::Value* keysPtr = ctx.builder.CreateLoad(llvm::PointerType::get(ctx.context, 0), ctx.builder.CreateStructGEP(mapType, mapPtr, 3), "keysBase");
        llvm::Value* valsPtr = ctx.builder.CreateLoad(llvm::PointerType::get(ctx.context, 0), ctx.builder.CreateStructGEP(mapType, mapPtr, 4), "valsBase");

        llvm::Type* keyType = keyVal->getType();
        llvm::Type* valType = valVal->getType();

        // Check if key exists loop
        llvm::Function* parentFunc = ctx.builder.GetInsertBlock()->getParent();
        llvm::BasicBlock* loopBB = llvm::BasicBlock::Create(ctx.context, "put_loop", parentFunc);
        llvm::BasicBlock* checkBB = llvm::BasicBlock::Create(ctx.context, "put_check", parentFunc);
        llvm::BasicBlock* foundBB = llvm::BasicBlock::Create(ctx.context, "put_found", parentFunc);
        llvm::BasicBlock* nextIterBB = llvm::BasicBlock::Create(ctx.context, "put_next", parentFunc);
        llvm::BasicBlock* notFoundBB = llvm::BasicBlock::Create(ctx.context, "put_not_found", parentFunc);
        llvm::BasicBlock* endBB = llvm::BasicBlock::Create(ctx.context, "put_end", parentFunc);
        
        llvm::Value* iVar = ctx.builder.CreateAlloca(ctx.builder.getInt32Ty(), nullptr, "i_put");
        ctx.builder.CreateStore(ctx.builder.getInt32(0), iVar);

        ctx.builder.CreateBr(loopBB);

        ctx.builder.SetInsertPoint(loopBB);
        llvm::Value* iVal = ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), iVar);
        llvm::Value* cond = ctx.builder.CreateICmpSLT(iVal, sizeVal);
        ctx.builder.CreateCondBr(cond, checkBB, notFoundBB);

        ctx.builder.SetInsertPoint(checkBB);
        // Load key[i]
        llvm::Value* keySlot = ctx.builder.CreateInBoundsGEP(keyType, keysPtr, iVal);
        llvm::Value* existingKey = ctx.builder.CreateLoad(keyType, keySlot);
        
        // Compare
        llvm::Value* code = compareKeys(ctx, keyVal, existingKey, keyType);
        ctx.builder.CreateCondBr(code, foundBB, nextIterBB);
        
        ctx.builder.SetInsertPoint(nextIterBB);
        llvm::Value* nextI = ctx.builder.CreateAdd(iVal, ctx.builder.getInt32(1));
        ctx.builder.CreateStore(nextI, iVar);
        ctx.builder.CreateBr(loopBB);

        // Found: Update Value
        ctx.builder.SetInsertPoint(foundBB);
        llvm::Value* valSlot = ctx.builder.CreateInBoundsGEP(valType, valsPtr, iVal);
        ctx.builder.CreateStore(valVal, valSlot);
        ctx.builder.CreateBr(endBB);

        // Not Found: Append
        ctx.builder.SetInsertPoint(notFoundBB);
        
        // Check Capacity
        llvm::BasicBlock* growBB = llvm::BasicBlock::Create(ctx.context, "put_grow", parentFunc);
        llvm::BasicBlock* appendBB = llvm::BasicBlock::Create(ctx.context, "put_append", parentFunc);
        
        llvm::Value* isFull = ctx.builder.CreateICmpEQ(sizeVal, capVal);
        ctx.builder.CreateCondBr(isFull, growBB, appendBB);
        
        ctx.builder.SetInsertPoint(growBB);
        // Simple grow: Realloc keys and values
        llvm::Value* newCap = ctx.builder.CreateMul(capVal, ctx.builder.getInt32(2));
        ctx.builder.CreateStore(newCap, capPtr);
        
        llvm::Function* reallocFunc = ctx.getFunction("realloc", llvm::PointerType::get(ctx.context, 0), 
                {llvm::PointerType::get(ctx.context, 0), ctx.builder.getInt64Ty()});
                
        // Keys realloc
        llvm::Value* keyOne = ctx.builder.CreateConstInBoundsGEP1_32(keyType, llvm::Constant::getNullValue(keyType->getPointerTo()), 1);
        llvm::Value* keySize = ctx.builder.CreatePtrToInt(keyOne, ctx.builder.getInt64Ty());
        llvm::Value* newKeysSize = ctx.builder.CreateMul(keySize, ctx.builder.CreateZExt(newCap, ctx.builder.getInt64Ty()));
        
        llvm::Value* keysMember = ctx.builder.CreateStructGEP(mapType, mapPtr, 3);
        llvm::Value* oldKeys = ctx.builder.CreateLoad(llvm::PointerType::get(ctx.context, 0), keysMember);
        llvm::Value* newKeys = ctx.builder.CreateCall(reallocFunc, {oldKeys, newKeysSize});
        ctx.builder.CreateStore(newKeys, keysMember);
        
        // Values realloc
        llvm::Value* valOne = ctx.builder.CreateConstInBoundsGEP1_32(valType, llvm::Constant::getNullValue(valType->getPointerTo()), 1);
        llvm::Value* valSize = ctx.builder.CreatePtrToInt(valOne, ctx.builder.getInt64Ty());
        llvm::Value* newValsSize = ctx.builder.CreateMul(valSize, ctx.builder.CreateZExt(newCap, ctx.builder.getInt64Ty()));
        
        llvm::Value* valsMember = ctx.builder.CreateStructGEP(mapType, mapPtr, 4);
        llvm::Value* oldVals = ctx.builder.CreateLoad(llvm::PointerType::get(ctx.context, 0), valsMember);
        llvm::Value* newVals = ctx.builder.CreateCall(reallocFunc, {oldVals, newValsSize});
        ctx.builder.CreateStore(newVals, valsMember);
        
        ctx.builder.CreateBr(appendBB);
        
        ctx.builder.SetInsertPoint(appendBB);
        // Reload bases
        llvm::Value* keysBase = ctx.builder.CreateLoad(llvm::PointerType::get(ctx.context, 0), ctx.builder.CreateStructGEP(mapType, mapPtr, 3));
        llvm::Value* valsBase = ctx.builder.CreateLoad(llvm::PointerType::get(ctx.context, 0), ctx.builder.CreateStructGEP(mapType, mapPtr, 4));
        
        // Reload Size (it hasn't changed, but just to be safe if optimized away)
        // Actually sizeVal is cached from entry, it is valid as long as we don't change size before here.
        
        llvm::Value* keyDest = ctx.builder.CreateInBoundsGEP(keyType, keysBase, sizeVal);
        ctx.builder.CreateStore(keyVal, keyDest);
        
        llvm::Value* valDest = ctx.builder.CreateInBoundsGEP(valType, valsBase, sizeVal);
        ctx.builder.CreateStore(valVal, valDest);
        
        llvm::Value* newSize = ctx.builder.CreateAdd(sizeVal, ctx.builder.getInt32(1));
        ctx.builder.CreateStore(newSize, sizePtr);
        
        ctx.builder.CreateBr(endBB);
        
        ctx.builder.SetInsertPoint(endBB);
    }

    llvm::Value* get(CompilerContext& ctx, llvm::Value* mapPtr, llvm::Value* keyVal, llvm::Type* valType, llvm::StructType* mapType) {
        // Return Value* (loaded) or default (0)
        llvm::Value* sizeVal = ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), ctx.builder.CreateStructGEP(mapType, mapPtr, 0));
        llvm::Value* keysPtr = ctx.builder.CreateLoad(llvm::PointerType::get(ctx.context, 0), ctx.builder.CreateStructGEP(mapType, mapPtr, 3));
        llvm::Value* valsPtr = ctx.builder.CreateLoad(llvm::PointerType::get(ctx.context, 0), ctx.builder.CreateStructGEP(mapType, mapPtr, 4));

        llvm::Type* keyType = keyVal->getType();

        llvm::Function* parentFunc = ctx.builder.GetInsertBlock()->getParent();
        llvm::BasicBlock* loopBB = llvm::BasicBlock::Create(ctx.context, "get_loop", parentFunc);
        llvm::BasicBlock* checkBB = llvm::BasicBlock::Create(ctx.context, "get_check", parentFunc);
        llvm::BasicBlock* foundBB = llvm::BasicBlock::Create(ctx.context, "get_found", parentFunc);
        llvm::BasicBlock* endBB = llvm::BasicBlock::Create(ctx.context, "get_end", parentFunc);
        
        llvm::Value* iVar = ctx.builder.CreateAlloca(ctx.builder.getInt32Ty(), nullptr, "i_get");
        ctx.builder.CreateStore(ctx.builder.getInt32(0), iVar);
        
        // Result var
        llvm::Value* resVar = ctx.builder.CreateAlloca(valType, nullptr, "res_get");
        // Default value: 0 or empty string?
        // use generic null
        ctx.builder.CreateStore(llvm::Constant::getNullValue(valType), resVar);

        ctx.builder.CreateBr(loopBB);
        
        ctx.builder.SetInsertPoint(loopBB);
        llvm::Value* iVal = ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), iVar);
        llvm::Value* cond = ctx.builder.CreateICmpSLT(iVal, sizeVal);
        ctx.builder.CreateCondBr(cond, checkBB, endBB); // If not found, go to end (returns default)

        ctx.builder.SetInsertPoint(checkBB);
        llvm::Value* keySlot = ctx.builder.CreateInBoundsGEP(keyType, keysPtr, iVal);
        llvm::Value* existingKey = ctx.builder.CreateLoad(keyType, keySlot);
        
        llvm::Value* code = compareKeys(ctx, keyVal, existingKey, keyType);
        
        llvm::BasicBlock* nextIterBB = llvm::BasicBlock::Create(ctx.context, "get_next", parentFunc);
        ctx.builder.CreateCondBr(code, foundBB, nextIterBB);
        
        ctx.builder.SetInsertPoint(foundBB);
        llvm::Value* valSlot = ctx.builder.CreateInBoundsGEP(valType, valsPtr, iVal);
        llvm::Value* val = ctx.builder.CreateLoad(valType, valSlot);
        ctx.builder.CreateStore(val, resVar);
        ctx.builder.CreateBr(endBB);
        
        ctx.builder.SetInsertPoint(nextIterBB);
        llvm::Value* nextI = ctx.builder.CreateAdd(iVal, ctx.builder.getInt32(1));
        ctx.builder.CreateStore(nextI, iVar);
        ctx.builder.CreateBr(loopBB);
        
        ctx.builder.SetInsertPoint(endBB);
        return ctx.builder.CreateLoad(valType, resVar, "get_res");
    }

    llvm::Value* contains(CompilerContext& ctx, llvm::Value* mapPtr, llvm::Value* keyVal, llvm::StructType* mapType) {
        llvm::Value* sizeVal = ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), ctx.builder.CreateStructGEP(mapType, mapPtr, 0));
        llvm::Value* keysPtr = ctx.builder.CreateLoad(llvm::PointerType::get(ctx.context, 0), ctx.builder.CreateStructGEP(mapType, mapPtr, 3));
        
        llvm::Type* keyType = keyVal->getType();
        
        llvm::Function* parentFunc = ctx.builder.GetInsertBlock()->getParent();
        llvm::BasicBlock* loopBB = llvm::BasicBlock::Create(ctx.context, "cont_loop", parentFunc);
        llvm::BasicBlock* checkBB = llvm::BasicBlock::Create(ctx.context, "cont_check", parentFunc);
        llvm::BasicBlock* foundBB = llvm::BasicBlock::Create(ctx.context, "cont_found", parentFunc);
        llvm::BasicBlock* endBB = llvm::BasicBlock::Create(ctx.context, "cont_end", parentFunc);
        
        llvm::Value* iVar = ctx.builder.CreateAlloca(ctx.builder.getInt32Ty(), nullptr, "i_cont");
        ctx.builder.CreateStore(ctx.builder.getInt32(0), iVar);
        
        // Result i1
        llvm::Value* resVar = ctx.builder.CreateAlloca(ctx.builder.getInt1Ty(), nullptr, "res_cont");
        ctx.builder.CreateStore(ctx.builder.getInt1(false), resVar);
        
        ctx.builder.CreateBr(loopBB);
        
        ctx.builder.SetInsertPoint(loopBB);
        llvm::Value* iVal = ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), iVar);
        llvm::Value* cond = ctx.builder.CreateICmpSLT(iVal, sizeVal);
        ctx.builder.CreateCondBr(cond, checkBB, endBB);
        
        ctx.builder.SetInsertPoint(checkBB);
        llvm::Value* keySlot = ctx.builder.CreateInBoundsGEP(keyType, keysPtr, iVal);
        llvm::Value* existingKey = ctx.builder.CreateLoad(keyType, keySlot);
        llvm::Value* code = compareKeys(ctx, keyVal, existingKey, keyType);
        
        llvm::BasicBlock* nextIterBB = llvm::BasicBlock::Create(ctx.context, "cont_next", parentFunc);
        ctx.builder.CreateCondBr(code, foundBB, nextIterBB);
        
        ctx.builder.SetInsertPoint(foundBB);
        ctx.builder.CreateStore(ctx.builder.getInt1(true), resVar);
        ctx.builder.CreateBr(endBB);
        
        ctx.builder.SetInsertPoint(nextIterBB);
        llvm::Value* nextI = ctx.builder.CreateAdd(iVal, ctx.builder.getInt32(1));
        ctx.builder.CreateStore(nextI, iVar);
        ctx.builder.CreateBr(loopBB);
        
        ctx.builder.SetInsertPoint(endBB);
        return ctx.builder.CreateLoad(ctx.builder.getInt1Ty(), resVar);
    }
    
    void remove(CompilerContext& ctx, llvm::Value* mapPtr, llvm::Value* keyVal, llvm::Type* valType, llvm::StructType* mapType) {
        llvm::Value* sizePtr = ctx.builder.CreateStructGEP(mapType, mapPtr, 0, "sizePtr");
        llvm::Value* sizeVal = ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), sizePtr);
        
        llvm::Value* keysPtr = ctx.builder.CreateLoad(llvm::PointerType::get(ctx.context, 0), ctx.builder.CreateStructGEP(mapType, mapPtr, 3));
        llvm::Value* valsPtr = ctx.builder.CreateLoad(llvm::PointerType::get(ctx.context, 0), ctx.builder.CreateStructGEP(mapType, mapPtr, 4));
        
        llvm::Type* keyType = keyVal->getType();

        llvm::Function* parentFunc = ctx.builder.GetInsertBlock()->getParent();
        llvm::BasicBlock* loopBB = llvm::BasicBlock::Create(ctx.context, "rm_loop", parentFunc);
        llvm::BasicBlock* checkBB = llvm::BasicBlock::Create(ctx.context, "rm_check", parentFunc);
        llvm::BasicBlock* foundBB = llvm::BasicBlock::Create(ctx.context, "rm_found", parentFunc);
        llvm::BasicBlock* endBB = llvm::BasicBlock::Create(ctx.context, "rm_end", parentFunc);
        
        llvm::Value* iVar = ctx.builder.CreateAlloca(ctx.builder.getInt32Ty(), nullptr, "i_rm");
        ctx.builder.CreateStore(ctx.builder.getInt32(0), iVar);

        ctx.builder.CreateBr(loopBB);

        ctx.builder.SetInsertPoint(loopBB);
        llvm::Value* iVal = ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), iVar);
        llvm::Value* cond = ctx.builder.CreateICmpSLT(iVal, sizeVal);
        ctx.builder.CreateCondBr(cond, checkBB, endBB);

        ctx.builder.SetInsertPoint(checkBB);
        llvm::Value* keySlot = ctx.builder.CreateInBoundsGEP(keyType, keysPtr, iVal);
        llvm::Value* existingKey = ctx.builder.CreateLoad(keyType, keySlot);
        llvm::Value* code = compareKeys(ctx, keyVal, existingKey, keyType);
        
        llvm::BasicBlock* nextIterBB = llvm::BasicBlock::Create(ctx.context, "rm_next", parentFunc);
        ctx.builder.CreateCondBr(code, foundBB, nextIterBB);

        ctx.builder.SetInsertPoint(foundBB);
        // Found: Shift [i+1, size] to [i]
        
        llvm::Value* nextIdx = ctx.builder.CreateAdd(iVal, ctx.builder.getInt32(1));
        llvm::Value* count = ctx.builder.CreateSub(sizeVal, nextIdx); // Items to move
        
        // Move Keys
        llvm::Value* keyDest = ctx.builder.CreateInBoundsGEP(keyType, keysPtr, iVal);
        llvm::Value* keySrc = ctx.builder.CreateInBoundsGEP(keyType, keysPtr, nextIdx);
        
        llvm::Value* keyOne = ctx.builder.CreateConstInBoundsGEP1_32(keyType, llvm::Constant::getNullValue(keyType->getPointerTo()), 1);
        llvm::Value* keySize = ctx.builder.CreatePtrToInt(keyOne, ctx.builder.getInt64Ty());
        llvm::Value* keyBytes = ctx.builder.CreateMul(ctx.builder.CreateZExt(count, ctx.builder.getInt64Ty()), keySize);
        
        llvm::Function* memmoveFunc = ctx.getFunction("memmove", llvm::PointerType::get(ctx.context, 0), 
             {llvm::PointerType::get(ctx.context, 0), llvm::PointerType::get(ctx.context, 0), ctx.builder.getInt64Ty()});
             
        ctx.builder.CreateCall(memmoveFunc, {keyDest, keySrc, keyBytes});
        
        // Move Values
        llvm::Value* valDest = ctx.builder.CreateInBoundsGEP(valType, valsPtr, iVal);
        llvm::Value* valSrc = ctx.builder.CreateInBoundsGEP(valType, valsPtr, nextIdx);
        
        llvm::Value* valOne = ctx.builder.CreateConstInBoundsGEP1_32(valType, llvm::Constant::getNullValue(valType->getPointerTo()), 1);
        llvm::Value* elemsSize = ctx.builder.CreatePtrToInt(valOne, ctx.builder.getInt64Ty());
        llvm::Value* valBytes = ctx.builder.CreateMul(ctx.builder.CreateZExt(count, ctx.builder.getInt64Ty()), elemsSize);
        
        ctx.builder.CreateCall(memmoveFunc, {valDest, valSrc, valBytes});
        
        // Decrement Size
        llvm::Value* newSize = ctx.builder.CreateSub(sizeVal, ctx.builder.getInt32(1));
        ctx.builder.CreateStore(newSize, sizePtr);
        
        ctx.builder.CreateBr(endBB);
        
        ctx.builder.SetInsertPoint(nextIterBB);
        // RECALCULATE nextI here because nextIdx from foundBB does not dominate this block!
        llvm::Value* nextI = ctx.builder.CreateAdd(iVal, ctx.builder.getInt32(1));
        ctx.builder.CreateStore(nextI, iVar); // i++
        ctx.builder.CreateBr(loopBB);
        
        ctx.builder.SetInsertPoint(endBB);
    }

}
