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
    
    llvm::StructType* getListStructType(CompilerContext& ctx);
    void initList(CompilerContext& ctx, llvm::Value* listPtr, llvm::Type* elemType, int initialCapacity = 4);
    void push(CompilerContext& ctx, llvm::Value* listPtr, llvm::Value* val);
    llvm::Value* pop(CompilerContext& ctx, llvm::Value* listPtr, llvm::Type* elemType);
    llvm::Value* get(CompilerContext& ctx, llvm::Value* listPtr, llvm::Value* indexVal, llvm::Type* elemType);
    llvm::Value* getElementPtr(CompilerContext& ctx, llvm::Value* listPtr, llvm::Value* indexVal, llvm::Type* elemType);
    llvm::Value* slice(CompilerContext& ctx, llvm::Value* listPtr, llvm::Value* startVal, llvm::Value* endVal, llvm::Type* elemType);
    llvm::Value* getSize(CompilerContext& ctx, llvm::Value* listPtr);
    llvm::Value* isEmpty(CompilerContext& ctx, llvm::Value* listPtr);
    void clear(CompilerContext& ctx, llvm::Value* listPtr);
    void insert(CompilerContext& ctx, llvm::Value* listPtr, llvm::Value* indexVal, llvm::Value* val);
    void remove(CompilerContext& ctx, llvm::Value* listPtr, llvm::Value* indexVal, llvm::Type* elemType);
} // namespace ListImpl
