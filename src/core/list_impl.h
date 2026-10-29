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
    // spec/61 §7.0 — append a value-MODEL element by DEEP-COPYING the source model at `srcAddr`
    // into a fresh (zero-initialised) slot, so the list element owns an INDEPENDENT heap. Used
    // when the pushed model is NOT a movable owned local (an `at`-expr, a member/param field, or a
    // local already moved) → otherwise a shallow struct store would share the source's inner heap
    // and the owner drops would double-free it. `serNodes`/`serCount` describe the element model.
    void pushDeepCopyModel(CompilerContext& ctx, llvm::Value* listPtr, llvm::Value* srcAddr,
                           llvm::Type* elemType, llvm::Value* serNodes, int serCount);
    llvm::Value* pop(CompilerContext& ctx, llvm::Value* listPtr, llvm::Type* elemType);
    llvm::Value* get(CompilerContext& ctx, llvm::Value* listPtr, llvm::Value* indexVal, llvm::Type* elemType);
    llvm::Value* getElementPtr(CompilerContext& ctx, llvm::Value* listPtr, llvm::Value* indexVal, llvm::Type* elemType);
    llvm::Value* slice(CompilerContext& ctx, llvm::Value* listPtr, llvm::Value* startVal, llvm::Value* endVal, llvm::Type* elemType, const char* retainFnName = nullptr);
    llvm::Value* getSize(CompilerContext& ctx, llvm::Value* listPtr);
    llvm::Value* isEmpty(CompilerContext& ctx, llvm::Value* listPtr);
    void clear(CompilerContext& ctx, llvm::Value* listPtr);
    void insert(CompilerContext& ctx, llvm::Value* listPtr, llvm::Value* indexVal, llvm::Value* val);
    void remove(CompilerContext& ctx, llvm::Value* listPtr, llvm::Value* indexVal, llvm::Type* elemType, bool elemIsString = false);

    // ── List-Builtins (doc/ideas/list_builtins_plan.md) ─────────────────────
    // M1: search + bulk-append.  indexOf returns i32 (first match, -1 if none);
    // count returns i32; extend appends value-copies of src into dst (snapshotting
    // src length up front so self.extend(self) and realloc are safe).
    llvm::Value* indexOf(CompilerContext& ctx, llvm::Value* listPtr, llvm::Value* needle,
                         llvm::Type* elemType, const std::string& elemTypeName);
    llvm::Value* count(CompilerContext& ctx, llvm::Value* listPtr, llvm::Value* needle,
                       llvm::Type* elemType, const std::string& elemTypeName);
    // retainFnName/serNodes (spec/10 "value-copies" + spec/61 §5.4): the appended copies must
    // CO-OWN each rc-bearing element, exactly as `slice` does — pass the element's incref
    // (aiml_str_incref / aiml_model_incref / aiml_{list,map,buffer}_handle_incref), or, for an
    // inline value model, its ser-node descriptor so each copied struct's heap fields are retained.
    // Both nullptr = scalar elements (no retain).
    void extend(CompilerContext& ctx, llvm::Value* dstPtr, llvm::Value* srcPtr, llvm::Type* elemType,
                const char* retainFnName = nullptr, llvm::Value* serNodes = nullptr,
                int32_t serNodesCount = 0);
    // M2: in-place ordering.  sortList = stable O(n log n) sort in the runtime (ascending or
    // descending); shuffle = Fisher-Yates via aiml_math_random_int.
    void reverse(CompilerContext& ctx, llvm::Value* listPtr, llvm::Type* elemType);
    void sortList(CompilerContext& ctx, llvm::Value* listPtr, llvm::Type* elemType,
                  const std::string& elemTypeName, bool descending);
    void shuffle(CompilerContext& ctx, llvm::Value* listPtr, llvm::Type* elemType);
} // namespace ListImpl
