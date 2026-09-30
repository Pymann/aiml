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
    // relKind (spec/61 §5.5/§5.7 value release): 0 none, 1 aiml_ser_free_decoded(slot) (inline model fields /
    // container handle slot), 2 aiml_model_decref(value) (shared model), 3 owning ptr<T> box (fields + shell).
    // Strings use valIsString.
    void put(CompilerContext& ctx, llvm::Value* mapPtr, llvm::Value* keyVal, llvm::Value* valVal, llvm::StructType* mapType, bool valIsString = false,
             llvm::Value* relDesc = nullptr, int relN = 0, int relKind = 1);
    // slotAddr: return the value SLOT address (null when missing) — the §5.4 borrow view for ref<V>.
    llvm::Value* get(CompilerContext& ctx, llvm::Value* mapPtr, llvm::Value* keyVal, llvm::Type* valType, llvm::StructType* mapType, bool slotAddr = false);
    llvm::Value* contains(CompilerContext& ctx, llvm::Value* mapPtr, llvm::Value* keyVal, llvm::StructType* mapType);
    void remove(CompilerContext& ctx, llvm::Value* mapPtr, llvm::Value* keyVal, llvm::Type* valType, llvm::StructType* mapType, bool valIsString = false,
                llvm::Value* relDesc = nullptr, int relN = 0, int relKind = 0);
} // namespace MapImpl
