#pragma once
#include "command.h"
#include "../core/expression.h"
#include "../core/context.h"
#include "registry.h" // Need valid type for registry access?
// Circular dependency: IfCommand -> Registry -> IfCommand
// We can use forward int declaration of CommandRegistry in Context.h, 
// and here we just need to know that ctx.registry is a CommandRegistry*.
// But we need the definition of CommandRegistry to call create().
// CommandRegistry includes if_command.h.
// So if_command.h cannot include registry.h
// Soloution: Abstract base class or Template?
// Or simply putting the recursion logic in a non-header file?
// Valid C++:
// Forward declare CommandRegistry in if_command.h?
// But we need to call `create`.
// Better: CommandRegistry is passed as `void*` or similar in Context? No.
// 
// Let's make `CommandRegistry` definition available.
// If `registry.h` includes `if_command.h`, `if_command.h` cannot include `registry.h`.
// 
// WE NEED A SEPARATE "CompilerUtils" or similar that handles the recursion loop.
// 
// actually, `CompilerContext` has `registry`.
// We can forward declare `CommandRegistry` in `if_command.h`.
// Then in `if_command.cpp` (if we had one) we include `registry.h`.
// But we are header-only for commands.
// 
// WORKAROUND: 
// Define `IfCommand` in a separate file, but logic in `compiler.cpp`? No.
// 
// NEW PLAN: 
// 1. `CommandRegistry` interface (ICommandFactory) defined in `command.h`?
// 2. `CommandRegistry` implements it.
// 3. `CompilerContext` holds `ICommandFactory*`.
// 
// Let's modify `command.h` to include a helper for recursive compilation?
// 
// Or simpler:
// Put `IfCommand` logic in a template or just accept that we need to defer the include?
// 
// Let's try to define `recursiveCompile(ctx, node)` in a separate header `compile_utils.h`
// `compile_utils.h` includes `registry.h`.
// `registry.h` includes `if_command.h`.
// `if_command.h` includes `compile_utils.h`. -> Cycle.

// OK. The simplest way is to have `IfCommand` NOT know about the Registry directly, 
// but use a callback passed in context?
// `CompilerContext` already has `Evaluator` (std::function).
// Let's add `Compiler` (std::function<void(Context&, Node&)>) to `CompilerContext`.

class IfCommand : public AIMLCommand {
public:
    void compile(CompilerContext& ctx, const YAML::Node& node) override {
        auto ifNode = node["if"];
        
        // ... Condition evaluation ...
        llvm::Value* condVal = Expression::evaluate(ctx, ifNode["condition"]);
        if (!condVal) return;
        
        // Ensure i1
        if (condVal->getType() != ctx.builder.getInt1Ty()) {
             condVal = ctx.builder.CreateICmpNE(condVal, llvm::Constant::getNullValue(condVal->getType()), "to_bool");
        }
        
        llvm::Function* parentFunc = ctx.builder.GetInsertBlock()->getParent();
        
        llvm::BasicBlock* thenBB = llvm::BasicBlock::Create(ctx.context, "if_then", parentFunc);
        llvm::BasicBlock* elseBB = llvm::BasicBlock::Create(ctx.context, "if_else");
        llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(ctx.context, "if_merge");
        
        bool hasElse = ifNode["else"] && ifNode["else"].IsSequence();
        
        ctx.builder.CreateCondBr(condVal, thenBB, hasElse ? elseBB : mergeBB);
        
        // THEN
        ctx.builder.SetInsertPoint(thenBB);
        if (ifNode["then"].IsSequence()) {
             // CALL RECURSIVE COMPILE
             if (ctx.params.compileCallback) {
                  ctx.params.compileCallback(ctx, ifNode["then"]);
             }
        }
        ctx.builder.CreateBr(mergeBB);
        
        // ELSE
        if (hasElse) {
             parentFunc->insert(parentFunc->end(), elseBB);
             ctx.builder.SetInsertPoint(elseBB);
             if (ctx.params.compileCallback) {
                  ctx.params.compileCallback(ctx, ifNode["else"]);
             }
             ctx.builder.CreateBr(mergeBB);
        } else {
             // delete elseBB? No, just don't insert it if not used? 
             // We created it but didn't insert it. LLVM requires manual cleanup if not inserted?
             // Actually `Create` without parent inserts it if parent provided.
             // We provided parent for `thenBB` but not `elseBB`.
             // `elseBB` needs to be inserted if used.
             delete elseBB; 
        }
        
        parentFunc->insert(parentFunc->end(), mergeBB);
        ctx.builder.SetInsertPoint(mergeBB);
    }
};

// I need to update Context to have `compileCallback`.
