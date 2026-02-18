#pragma once
#include "command.h"
#include "../core/expression.h"
#include "../core/context.h"

class StoreCommand : public AIMLCommand {
public:
    void compile(CompilerContext& ctx, const YAML::Node& node) override {
        // Syntax: store: [ptrExpr, valExpr, (optional)depth]
        // Example: store: [p, 20] (depth 1, default) -> *p = 20
        // Example: store: [pp, 20, 2] -> *(*pp) = 20
        
        auto args = node["store"];
        if (!args.IsSequence() || args.size() < 2) {
            LOG_ERROR("Store command requires [ptr, value, (opt)depth]");
            return;
        }
        
        llvm::Value* ptrVal = Expression::evaluate(ctx, args[0]);
        llvm::Value* valVal = Expression::evaluate(ctx, args[1]);
        
        int depth = 1;
        if (args.size() > 2) {
            depth = args[2].as<int>();
        }
        
        if (depth < 1) {
             LOG_ERROR("Store depth must be >= 1");
             return;
        }
        
        if (!ptrVal || !valVal) {
             LOG_ERROR("Store operands evaluated to null");
             return;
        }
        
        // Traverse pointers check
        // depth=1 -> *p = val. (0 loads)
        // depth=2 -> **p = *(*p) = val. (1 load)
        
        // Traverse pointers if depth > 1
        llvm::Value* currentPtr = ptrVal;
        for (int i = 0; i < depth - 1; ++i) {
             if (!currentPtr->getType()->isPointerTy()) {
                 LOG_ERROR("Store depth traversal encountered non-pointer at depth " + std::to_string(i));
                 return;
             }
             
             // We need to load the next pointer.
             // Issue: Opaque pointers don't know the type to load.
             // We need metadata again?
             // Or we assume `ptr` (pointer types are opaque, but we load a pointer).
             // Since we are traversing pointers, we are loading an address (pointer).
             // LLVM IR: `load ptr, ptr %p` is valid.
             // So we use keys: `ctx.builder.getPtrTy()` (LLVM 15+ opaque) or `PointerType::get(...)`
             
             currentPtr = ctx.builder.CreateLoad(llvm::PointerType::get(ctx.context, 0), currentPtr, "store_traverse");
        }
        
        if (!currentPtr->getType()->isPointerTy()) {
             LOG_ERROR("Store target is not a pointer");
             return;
        }
        
        ctx.builder.CreateStore(valVal, currentPtr);
    }
};
