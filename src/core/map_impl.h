#pragma once
#include "context.h"
#include <llvm/IR/IRBuilder.h>

namespace MapImpl {

    // Struct Layout: { i32 size, i32 capacity, i8* occupied, K* keys, V* values }
    // Indices: 0=size, 1=capacity, 2=occupied, 3=keys, 4=values

    llvm::StructType* getMapStructType(CompilerContext& ctx, llvm::Type* keyType, llvm::Type* valType);
    void initMap(CompilerContext& ctx, llvm::Value* mapPtr, llvm::Type* keyType, llvm::Type* valType, int initialCapacity = 4);
    llvm::Value* compareKeys(CompilerContext& ctx, llvm::Value* k1, llvm::Value* k2, llvm::Type* keyType);
    llvm::Value* getSize(CompilerContext& ctx, llvm::Value* mapPtr, llvm::StructType* mapType);
    void put(CompilerContext& ctx, llvm::Value* mapPtr, llvm::Value* keyVal, llvm::Value* valVal, llvm::StructType* mapType);
    llvm::Value* get(CompilerContext& ctx, llvm::Value* mapPtr, llvm::Value* keyVal, llvm::Type* valType, llvm::StructType* mapType);
    llvm::Value* contains(CompilerContext& ctx, llvm::Value* mapPtr, llvm::Value* keyVal, llvm::StructType* mapType);
    void remove(CompilerContext& ctx, llvm::Value* mapPtr, llvm::Value* keyVal, llvm::Type* valType, llvm::StructType* mapType);
} // namespace MapImpl
