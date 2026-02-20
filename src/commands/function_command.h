#pragma once
#include "command.h"
#include "../core/expression.h"
#include "../core/context.h"
#include <vector>

class FunctionCommand : public AIMLCommand {
public:
    void compile(CompilerContext& ctx, const YAML::Node& node) override {
        // Syntax: 
        // - def:
        //     name: func_name
        //     args: [[arg1, type1], [arg2, type2]]
        //     return: ret_type
        //     body: [...]
        
        auto defNode = node["def"];
        std::string rawName = defNode["name"].as<std::string>();
        std::string name = ctx.qualify(rawName); // Qualified Name
        
        uint64_t version = 0;
        if (defNode["version"]) {
             version = defNode["version"].as<uint64_t>();
        }
        
        // 1. Parse Return Type
        llvm::Type* retType = ctx.builder.getVoidTy();
        if (defNode["return"] && defNode["return"].IsScalar()) {
            std::string retStr = defNode["return"].as<std::string>();
            if (!retStr.empty() && retStr != "void") {
                 retType = ctx.getLLVMType(retStr);
            }
        }
        
        // 2. Parse Args
        std::vector<llvm::Type*> argTypes;
        std::vector<std::string> argNames;
        
        if (defNode["args"] && defNode["args"].IsSequence()) {
            for (const auto& argPair : defNode["args"]) {
                std::string argName = argPair[0].as<std::string>();
                std::string argTypeStr = argPair[1].as<std::string>();
                argTypes.push_back(ctx.getLLVMType(argTypeStr));
                argNames.push_back(argName);
            }
        }
        
        // 3. Create Function Type & Function
        llvm::FunctionType* funcType = llvm::FunctionType::get(retType, argTypes, false);
        llvm::Function* func = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, name, ctx.module);
        
        // Set Personality Function for Exception Handling
        func->setPersonalityFn(llvm::cast<llvm::Constant>(ctx.getPersonalityFunction().getCallee()));

        // Register Function Info with Version (Available even in header mode)
        CompilerContext::FunctionInfo funcInfo;
        funcInfo.func = func;
        funcInfo.argNames = argNames;
        funcInfo.version = version;
        ctx.functions[rawName] = funcInfo; // Register short name? OR qualified?
        // Register Qualified Name too?
        // Users might call `my.lib.func`.
        // CallCommand needs to resolve.
        ctx.functions[name] = funcInfo;

        if (ctx.headerOnlyMode) {
             LOG_DEBUG("HeaderOnly: Skipping body for " + name);
             return; 
        }
        
        // 4. Create Body
        llvm::BasicBlock* entryBB = llvm::BasicBlock::Create(ctx.context, "entry", func);
        
        // Save current IP
        llvm::BasicBlock* prevBlock = ctx.builder.GetInsertBlock();
        ctx.builder.SetInsertPoint(entryBB);
        
        // 5. Setup Args in Symbol Table (Alloca for mutability)
        // Note: New scope? simple implementation: just overwrite symbol table and restore?
        // Or cleaner: scoped symbol table?
        // Since we don't have true scopes yet, we can just save previous symbols?
        // Or simpler: functions are top-level, so no local vars from outside are visible anyway (except globals).
        // Since we don't distinguish globals/locals well yet, assume flat for now but shadow args.
        // Actually, symbols like `x`, `y` from main should NOT be visible?
        // But preventing that requires scoping.
        // For now: Just add args. If names collide with globals, we shadow them.
        // BUT: We need to cleanup after function!
        
        // Store old symbols to restore?
        // Since we are top-level "def", maybe we assume we are at global scope?
        // But `compiler.cpp` puts everything in `main`.
        // If we define function inside `main`, IR is valid but logic is nested.
        // LLVM functions are modules level.
        // So `impl` is generating a module-level function.
        // Symbols in `ctx.symbol_table` are currently for `main`'s allocas.
        // We should clear or push a scope.
        // Let's implement a simple "Scope Guard" or just save/restore specific args.
        // Given complexity, let's just insert args and not worry about cleanup for this iteration, 
        // relying on unique names or standard shadowing behavior.
        // Wait, if I access `x` from main inside func, it will try to access `main`'s alloca -> Error (Stack pointer from another function).
        // So we MUST NOT access main's symbols.
        // Correct fix: clear symbol table for function body, then restore.
        
        auto oldSymbols = ctx.symbol_table; // Copy! (Heavy but safe)
        ctx.symbol_table.clear(); 
        // We might want to keep globals if we had them. Currently everything is in main.
        
        int argIdx = 0;
        for (auto& arg : func->args()) {
            arg.setName(argNames[argIdx]);
            
            // Create alloca for arg
            llvm::IRBuilder<> tmpBuilder(&func->getEntryBlock(), func->getEntryBlock().begin());
            llvm::AllocaInst* alloca = tmpBuilder.CreateAlloca(argTypes[argIdx], nullptr, argNames[argIdx]);
            ctx.builder.CreateStore(&arg, alloca);
            
            Symbol sym;
            sym.alloca_inst = alloca;
            sym.type = argTypes[argIdx];
            
            // Resolve Class Name for Argument
            std::string argTypeStr = "";
            if (defNode["args"][argIdx][1].IsScalar()) {
                 argTypeStr = defNode["args"][argIdx][1].as<std::string>();
            }
            
            if (!argTypeStr.empty()) {
                 if (ctx.classes.count(argTypeStr)) {
                     sym.className = argTypeStr;
                 } else if ((argTypeStr.rfind("ptr<", 0) == 0 || argTypeStr.rfind("ref<", 0) == 0) && argTypeStr.back() == '>') {
                      std::string innerType;
                      if (argTypeStr.rfind("ptr<", 0) == 0) innerType = argTypeStr.substr(4, argTypeStr.length() - 5);
                      else innerType = argTypeStr.substr(4, argTypeStr.length() - 5);
                      
                      // Check access for innerType or if it's a class
                      if (ctx.classes.count(innerType)) {
                          sym.className = innerType;
                      }
                 }
            }
            
            ctx.symbol_table[argNames[argIdx]] = sym;
            
            argIdx++;
        }
        
        // 6. Compile Body
        if (defNode["body"] && defNode["body"].IsSequence()) {
             if (ctx.params.compileCallback) {
                  ctx.params.compileCallback(ctx, defNode["body"]);
             }
        }
        
        // 7. Implicit Void Return if missing
        if (retType->isVoidTy()) {
            if (!ctx.builder.GetInsertBlock()->getTerminator()) {
                 ctx.builder.CreateRetVoid();
            }
        } else {
             // If not void and no terminator, it's an error or undefined (0 typically)
             // We'll let LLVM verify catch it or add default 0 return?
             if (!ctx.builder.GetInsertBlock()->getTerminator()) {
                 if (retType->isIntegerTy())
                     ctx.builder.CreateRet(llvm::Constant::getNullValue(retType));
                 else
                     ctx.builder.CreateUnreachable(); // Should have returned
             }
        }
        
        // 8. Restore Context
        ctx.builder.SetInsertPoint(prevBlock);
        ctx.symbol_table = oldSymbols; // Restore
        
        LOG_DEBUG("Function " + name + " compiled.");
    }
};
