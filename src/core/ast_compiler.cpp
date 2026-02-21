#include "ast_compiler.h"
#include "list_impl.h"
#include "map_impl.h"
#include "buffer_impl.h"
#include "string_utils.h"
#include <iostream>

namespace AIML {
namespace AST {

void Compiler::compile(Program* prog) {
    if (!prog) return;
    for (const auto& decl : prog->declarations) {
        visitTopLevel(decl.get());
    }
}

void Compiler::visitTopLevel(Node* node) {
    if (auto func = dynamic_cast<FunctionDecl*>(node)) {
        // Prevent duplicate generation if legacy pipeline already compiled it
        std::string mangledName = func->name;
        if (ctx.functions.find(mangledName) != ctx.functions.end()) {
             // Already compiled by FunctionCommand. Skip.
             return;
        }
        visitFunction(func);
    } else if (auto stmt = dynamic_cast<Stmt*>(node)) {
        // TEMPORARY: Skip top-level statements as legacy `compiler.cpp` already processed them into `__aiml_init`.
        // visitStatement(stmt);
    }
}

void Compiler::visitFunction(FunctionDecl* func) {
    // Basic mapping using our old context structure for now
    std::string mangledName = func->name;
    
    std::vector<IRType*> argTypes;
    std::vector<std::string> argNames;
    
    for (const auto& arg : func->args) {
        IRType* t = ctx.ir_gen->resolveType(arg.second);
        if (t) {
            argTypes.push_back(t);
            argNames.push_back(arg.first);
        }
    }
    
    IRType* retTy = nullptr;
    if (!func->returnType.empty()) retTy = ctx.ir_gen->resolveType(func->returnType);
    if (!retTy) retTy = ctx.ir_gen->resolveType("void");
    
    IRValue* funcVal = ctx.ir_gen->createFunction(mangledName, retTy, argTypes);
    
    // Set Personality Function for exception handling
    if (ctx.ir_gen->getPersonalityFunction()) {
        ctx.ir_gen->setPersonalityFunction(funcVal, ctx.ir_gen->getPersonalityFunction());
    }

    // Register Function Info
    CompilerContext::FunctionInfo funcInfo;
    funcInfo.func = static_cast<llvm::Function*>(ctx.ir_gen->unwrap(funcVal));
    funcInfo.argNames = argNames;
    funcInfo.version = 0; // Not handling versioning in AST yet
    ctx.functions[func->name] = funcInfo;
    ctx.functions[mangledName] = funcInfo;

    // Header Only mode check
    if (ctx.headerOnlyMode) {
        LOG_DEBUG("HeaderOnly: Skipping body for " + mangledName);
        return; 
    }

    // Create Entry Block
    IRBlock* entryBB = ctx.ir_gen->createBasicBlock("entry");
    
    // In LLVM, BasicBlock::Create without a parent doesn't attach. 
    // Wait, createBasicBlock attaches to GetInsertBlock()->getParent().
    // We need a way to insert into the NEW function.
    // Let's add appendBasicBlock to IRGenerator or just use unwrap for now.
    llvm::Function* llvmFunc = funcInfo.func;
    llvm::BasicBlock* llvmEntryBB = static_cast<llvm::BasicBlock*>(ctx.ir_gen->unwrapBlock(entryBB));
    // If it was already attached to old parent, we must move it or create it cleanly.
    // Let's create it directly via LLVM for the transition phase to ensure it attaches to funcVal.
    llvmEntryBB = llvm::BasicBlock::Create(ctx.context, "entry", llvmFunc);
    
    // Save previous block and set to new entry
    IRBlock* oldBlock = ctx.ir_gen->getInsertBlock();
    ctx.ir_gen->setInsertPoint(ctx.ir_gen->wrapBlock(llvmEntryBB));

    // Setup Arguments in Symbol Table
    for (size_t i = 0; i < argNames.size(); ++i) {
        IRValue* argAlloca = ctx.ir_gen->createAlloca(argTypes[i], argNames[i]);
        IRValue* argVal = ctx.ir_gen->getFunctionArg(funcVal, i);
        ctx.ir_gen->createStore(argVal, argAlloca);

        Symbol sym;
        sym.ir_alloca_inst = argAlloca;
        sym.ir_type = ctx.ir_gen->getPointerTy();
        sym.ir_element_type = argTypes[i];
        sym.typeName = func->args[i].second;
        ctx.symbol_table[argNames[i]] = sym;
    }

    // Compile Body
    if (func->body) {
        visitBlockStmt(func->body.get());
    }

    // Check if the last block has a terminator (like ret)
    llvm::BasicBlock* currentBB = static_cast<llvm::BasicBlock*>(ctx.ir_gen->unwrapBlock(ctx.ir_gen->getInsertBlock()));
    if (!currentBB->getTerminator()) {
        llvm::Type* unwrappedRetTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(retTy));
        if (unwrappedRetTy->isVoidTy()) {
            ctx.ir_gen->createRet(nullptr);
        } else if (unwrappedRetTy->isIntegerTy()) {
            ctx.ir_gen->createRet(ctx.ir_gen->wrap(llvm::ConstantInt::get(unwrappedRetTy, 0)));
        } else if (unwrappedRetTy->isPointerTy()) {
            ctx.ir_gen->createRet(ctx.ir_gen->wrap(llvm::ConstantPointerNull::get(static_cast<llvm::PointerType*>(unwrappedRetTy))));
        } else if (unwrappedRetTy->isFloatingPointTy()) {
            ctx.ir_gen->createRet(ctx.ir_gen->wrap(llvm::ConstantFP::get(unwrappedRetTy, 0.0)));
        } else {
            ctx.ir_gen->createRet(ctx.ir_gen->wrap(llvm::UndefValue::get(unwrappedRetTy)));
        }
    }

    // Restore insertion point
    ctx.ir_gen->setInsertPoint(oldBlock);
}

void Compiler::visitStatement(Stmt* stmt) {
    if (auto decl = dynamic_cast<DeclStmt*>(stmt)) visitDeclStmt(decl);
    else if (auto setStmt = dynamic_cast<SetStmt*>(stmt)) visitSetStmt(setStmt);
    else if (auto exprStmt = dynamic_cast<ExprStmt*>(stmt)) visitExprStmt(exprStmt);
    else if (auto retStmt = dynamic_cast<RetStmt*>(stmt)) visitRetStmt(retStmt);
    else if (auto blockStmt = dynamic_cast<BlockStmt*>(stmt)) visitBlockStmt(blockStmt);
    else if (auto ifStmt = dynamic_cast<IfStmt*>(stmt)) visitIfStmt(ifStmt);
    else if (auto whileStmt = dynamic_cast<WhileStmt*>(stmt)) visitWhileStmt(whileStmt);
}

void Compiler::visitDeclStmt(DeclStmt* decl) {
    IRType* ty = ctx.ir_gen->resolveType(decl->typeName);
    if (!ty) return;
    
    IRValue* alloca = ctx.ir_gen->createAlloca(ty, decl->varName);
    
    Symbol sym;
    sym.ir_alloca_inst = alloca;
    sym.ir_type = ctx.ir_gen->getPointerTy();
    sym.ir_element_type = ty;
    sym.typeName = decl->typeName;
    ctx.symbol_table[decl->varName] = sym;
    
    LOG_DEBUG("Compiled AST Decl: " + decl->varName + " of " + decl->typeName);
    
    if (decl->initialValue) {
        IRValue* initVal = visitExpression(decl->initialValue.get());
        if (initVal) {
            ctx.ir_gen->createStore(initVal, alloca);
        }
    }
}

void Compiler::visitSetStmt(SetStmt* setStmt) {
    std::string targetStr = setStmt->target;
    
    // Support dot-notation (obj.prop) and dash-notation (arr-index)
    size_t dotPos = targetStr.find('.');
    size_t dashPos = targetStr.find('-');
    size_t sepPos = (dotPos != std::string::npos) ? dotPos : dashPos;
    bool isDash = (dashPos != std::string::npos && dotPos == std::string::npos);
    
    std::string baseName = (sepPos != std::string::npos) ? targetStr.substr(0, sepPos) : targetStr;
    std::string propName = (sepPos != std::string::npos) ? targetStr.substr(sepPos + 1) : "";

    auto it = ctx.symbol_table.find(baseName);
    if (it == ctx.symbol_table.end()) {
        std::cerr << "Undefined variable (set target): " << targetStr << "\n";
        return;
    }
    
    IRValue* baseAlloca = it->second.ir_alloca_inst ? it->second.ir_alloca_inst : ctx.ir_gen->wrap(it->second.alloca_inst);
    IRType* baseElemTy = it->second.ir_element_type ? it->second.ir_element_type : ctx.ir_gen->wrapType(it->second.element_type);
    
    if (!baseAlloca) return;
    
    IRValue* val = visitExpression(setStmt->value.get());
    if (!val) return;
    
    if (propName.empty()) {
        ctx.ir_gen->createStore(val, baseAlloca);
    } else {
        std::string className = it->second.typeName;
        if ((className.rfind("ptr<", 0) == 0 || className.rfind("ref<", 0) == 0) && className.back() == '>') {
             className = className.substr(4, className.length() - 5);
        }
        
        llvm::Type* llvmBaseElemTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(baseElemTy));
        if (isDash && (className.find("list<") == 0 || llvmBaseElemTy->isArrayTy())) {
            size_t colonPos = propName.find(':');
            if (colonPos != std::string::npos) {
                std::cerr << "Warning: Slice assignment is not supported yet: " << targetStr << "\n";
                return;
            }
            
            llvm::Value* idxVal = nullptr;
            try {
                idxVal = ctx.builder.getInt32(std::stoi(propName));
            } catch(...) {
                Symbol* idxSym = resolveVariable(propName);
                if (idxSym) {
                    IRValue* idxAlloca = idxSym->ir_alloca_inst ? idxSym->ir_alloca_inst : ctx.ir_gen->wrap(idxSym->alloca_inst);
                    IRType* idxElemTy = idxSym->ir_element_type ? idxSym->ir_element_type : ctx.ir_gen->wrapType(idxSym->element_type);
                    idxVal = ctx.builder.CreateLoad(static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(idxElemTy)), static_cast<llvm::Value*>(ctx.ir_gen->unwrap(idxAlloca)));
                    if (idxVal->getType()->isIntegerTy(64)) idxVal = ctx.builder.CreateTrunc(idxVal, ctx.builder.getInt32Ty());
                } else {
                    LOG_ERROR("Invalid list index for set: " + propName);
                    return;
                }
            }
            
            llvm::Value* elemPtr = nullptr;
            if (llvmBaseElemTy->isArrayTy()) {
                elemPtr = ctx.builder.CreateInBoundsGEP(llvmBaseElemTy, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), {ctx.builder.getInt32(0), idxVal});
            } else {
                elemPtr = ListImpl::getElementPtr(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), idxVal, llvmBaseElemTy);
            }
            ctx.builder.CreateStore(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val)), elemPtr);
            return;
        }
        
        if (!className.empty() && ctx.classes.count(className)) {
            auto& classInfo = ctx.classes[className];
            if (classInfo.members.count(propName)) {
                auto& member = classInfo.members[propName];
                IRType* structTy = ctx.ir_gen->wrapType(classInfo.type);
                IRValue* structPtr = baseAlloca;
                
                if (baseElemTy->isPointer()) {
                     structPtr = ctx.ir_gen->createLoad(baseElemTy, baseAlloca, baseName + "_ptr");
                }
                
                IRValue* gep = ctx.ir_gen->createStructGEP(structTy, structPtr, member.index, "set_prop_ptr");
                ctx.ir_gen->createStore(val, gep);
                return;
            }
        }
        std::cerr << "Undefined property for set: " << targetStr << "\n";
    }
}

void Compiler::visitExprStmt(ExprStmt* exprStmt) {
    visitExpression(exprStmt->expr.get());
}

void Compiler::visitRetStmt(RetStmt* retStmt) {
    if (retStmt->value) {
        IRValue* val = visitExpression(retStmt->value.get());
        if (val) ctx.ir_gen->createRet(val);
        else ctx.ir_gen->createRet(nullptr);
    } else {
        ctx.ir_gen->createRet(nullptr);
    }
}

void Compiler::visitBlockStmt(BlockStmt* block) {
    for (const auto& stmt : block->statements) {
        visitStatement(stmt.get());
    }
}

void Compiler::visitIfStmt(IfStmt* ifStmt) {
    if (!ifStmt->condition) return;
    
    IRValue* condVal = visitExpression(ifStmt->condition.get());
    if (!condVal) return;
    
    // Ensure the condition is boolean (i1). In AIML, any non-zero is true.
    // For now assume strictly typed, or cast:
    if (ctx.ir_gen->unwrapType(condVal->getType()) != ctx.builder.getInt1Ty()) {
        IRValue* zero = ctx.ir_gen->getInt32(0); // Assuming 32-bit compare for now
        condVal = ctx.ir_gen->createICmpNE(condVal, zero, "to_bool");
    }
    
    IRBlock* thenBB = ctx.ir_gen->createBasicBlock("if_then");
    IRBlock* mergeBB = nullptr;
    IRBlock* elseBB = nullptr;
    
    if (ifStmt->elseBranch) {
        elseBB = ctx.ir_gen->createBasicBlock("if_else");
    }
    mergeBB = ctx.ir_gen->createBasicBlock("if_merge");
    
    ctx.ir_gen->createCondBr(condVal, thenBB, ifStmt->elseBranch ? elseBB : mergeBB);
    
    // THEN branch
    ctx.ir_gen->setInsertPoint(thenBB);
    if (ifStmt->thenBranch) {
        visitBlockStmt(ifStmt->thenBranch.get());
    }
    ctx.ir_gen->createBr(mergeBB);
    
    // ELSE branch
    if (ifStmt->elseBranch) {
        ctx.ir_gen->setInsertPoint(elseBB);
        visitBlockStmt(ifStmt->elseBranch.get());
        ctx.ir_gen->createBr(mergeBB);
    }
    
    // MERGE block
    ctx.ir_gen->setInsertPoint(mergeBB);
}

void Compiler::visitWhileStmt(WhileStmt* whileStmt) {
    if (!whileStmt->condition) return;
    
    IRBlock* loopBB = ctx.ir_gen->createBasicBlock("while_loop");
    IRBlock* bodyBB = ctx.ir_gen->createBasicBlock("while_body");
    IRBlock* endBB = ctx.ir_gen->createBasicBlock("while_end");
    
    // Jump to loop check
    ctx.ir_gen->createBr(loopBB);
    
    // Loop Check
    ctx.ir_gen->setInsertPoint(loopBB);
    IRValue* condVal = visitExpression(whileStmt->condition.get());
    if (!condVal) return;
    
    if (ctx.ir_gen->unwrapType(condVal->getType()) != ctx.builder.getInt1Ty()) {
        IRValue* zero = ctx.ir_gen->getInt32(0);
        condVal = ctx.ir_gen->createICmpNE(condVal, zero, "while_cond_bool");
    }
    
    ctx.ir_gen->createCondBr(condVal, bodyBB, endBB);
    
    // Body Block
    ctx.ir_gen->setInsertPoint(bodyBB);
    if (whileStmt->body) {
        visitBlockStmt(whileStmt->body.get());
    }
    ctx.ir_gen->createBr(loopBB);
    
    // End Block
    ctx.ir_gen->setInsertPoint(endBB);
}

IRValue* Compiler::visitExpression(Expr* expr) {
    if (auto lit = dynamic_cast<LiteralExpr*>(expr)) return visitLiteral(lit);
    if (auto var = dynamic_cast<VariableExpr*>(expr)) return visitVariable(var);
    if (auto call = dynamic_cast<CallExpr*>(expr)) return visitCall(call);
    if (auto newExpr = dynamic_cast<NewExpr*>(expr)) return visitNewExpr(newExpr);
    if (auto interp = dynamic_cast<StringInterpolationExpr*>(expr)) return visitStringInterpolation(interp);
    if (auto bin = dynamic_cast<BinaryExpr*>(expr)) return visitBinary(bin);
    return nullptr;
}

IRValue* Compiler::visitStringInterpolation(StringInterpolationExpr* expr) {
    if (expr->parts.empty()) return ctx.ir_gen->createGlobalStringPtr("");
    
    llvm::Value* currentStr = nullptr;
    for (auto& part : expr->parts) {
        IRValue* irVal = visitExpression(part.get());
        if (!irVal) continue;
        
        llvm::Value* val = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(irVal));
        llvm::Type* ty = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(irVal->getType()));
        
        llvm::Value* strVal = val;
        // Basic type conversions to string
        if (ty->isIntegerTy(32)) {
            strVal = StringUtils::intToString(ctx, val);
        } else if (ty->isIntegerTy(64)) {
            strVal = StringUtils::int64ToString(ctx, val);
        } else if (ty->isIntegerTy(1)) {
            llvm::Value* cond = ctx.builder.CreateICmpEQ(val, ctx.builder.getInt32(1));
            llvm::Value* trueStr = ctx.builder.CreateGlobalStringPtr("true");
            llvm::Value* falseStr = ctx.builder.CreateGlobalStringPtr("false");
            strVal = ctx.builder.CreateSelect(cond, trueStr, falseStr);
        }
        
        if (!currentStr) currentStr = strVal;
        else currentStr = StringUtils::concat(ctx, currentStr, strVal);
    }
    
    return ctx.ir_gen->wrap(currentStr);
}

IRValue* Compiler::visitNewExpr(NewExpr* newExpr) {
    std::string className = newExpr->typeName;
    if (!className.empty() && ctx.classes.count(className)) {
        llvm::StructType* structTy = ctx.classes[className].type;
        uint64_t typeSize = ctx.module.getDataLayout().getTypeAllocSize(structTy);
        
        // Ensure calloc exists
        llvm::Function* callocFunc = ctx.module.getFunction("calloc");
        if (!callocFunc) {
             llvm::FunctionType* callocType = llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), {ctx.builder.getInt64Ty(), ctx.builder.getInt64Ty()}, false);
             llvm::Function::Create(callocType, llvm::Function::ExternalLinkage, "calloc", ctx.module);
        }
        
        std::vector<IRValue*> callArgs;
        // Assuming we add a way to get Int64. If not, Int32 will probably implicitly cast or we can use getInt32.
        // Actually `ast_compiler` can just use `ctx.ir_gen->wrap(ctx.builder.getInt64(...))` for literal bridging.
        callArgs.push_back(ctx.ir_gen->wrap(ctx.builder.getInt64(1)));
        callArgs.push_back(ctx.ir_gen->wrap(ctx.builder.getInt64(typeSize)));
        
        IRValue* rawPtr = ctx.ir_gen->createCall("calloc", callArgs, "calloc_call");
        
        // Auto-initialize defined members
        auto& classInfo = ctx.classes[className];
        for (auto& [name, member] : classInfo.members) {
             if (member.initValue.IsDefined() && !member.initValue.IsNull()) {
                  // Legacy evaluation bridge for AST... it's a bit dirty, wait!
                  // Ideally the AST `DeclStmt` doesn't have YAML nodes for `initValue`.
                  // Since `AST::ClassDecl` will eventually replace YAML nodes, we might skip initialization here
                  // in favor of generating initialization code in a constructor function.
                  // For now, let's keep it simple: skip YAML evaluation in AST Compiler for `new`.
                  // The user can just initialize themselves. Wait, the old compiler did `evaluate`.
                  // But `evaluator` evaluates YAML nodes! Since we are purely AST, we don't have evaluator here!
                  // We'll skip member defaults for the {new: Point} command since it's just allocation!
             }
        }
        return rawPtr;
    }
    
    LOG_ERROR("Unknown class for new: " + className);
    return ctx.ir_gen->getInt32(0);
}

IRValue* Compiler::visitLiteral(LiteralExpr* lit) {
    if (lit->type == "i32" || lit->type == "int") {
        try {
            return ctx.ir_gen->getInt32(std::stoi(lit->value, nullptr, 0));
        } catch (...) {
            return ctx.ir_gen->getInt32(0);
        }
    } else if (lit->type == "string") {
        return ctx.ir_gen->createGlobalStringPtr(lit->value);
    } else if (lit->type == "bool") {
        return ctx.ir_gen->getInt32(lit->value == "true" ? 1 : 0);
    } else if (lit->type == "unknown") {
        // Strict parsing for unquoted scalars that are numbers or booleans
        if (lit->value == "true") return ctx.ir_gen->getInt32(1);
        if (lit->value == "false") return ctx.ir_gen->getInt32(0);
        
        try {
            size_t pos = 0;
            int num = std::stoi(lit->value, &pos, 0);
            return ctx.ir_gen->getInt32(num);
        } catch (...) {
            LOG_ERROR("Invalid unknown literal (expected bool or number): " + lit->value);
            return ctx.ir_gen->getInt32(0);
        }
    }
    
    // Fallback stub
    LOG_DEBUG("Visit Unhandled Literal Type: " + lit->type + " Value: " + lit->value);
    return ctx.ir_gen->getInt32(0);
}

Symbol* Compiler::resolveVariable(const std::string& name) {
    if (ctx.currentScope) {
        CompilerContext::Scope* scope = ctx.currentScope;
        while (scope) {
            if (scope->variables.count(name)) return &scope->variables[name];
            scope = scope->parent;
        }
    }
    
    // Check namespace globals
    if (ctx.currentNs && ctx.currentNs->globals.count(name)) {
        return &ctx.currentNs->globals[name];
    }
    
    // Legacy fallback during transition
    if (ctx.symbol_table.count(name)) return &ctx.symbol_table[name];
    
    return nullptr;
}

IRValue* Compiler::resolveEnum(const std::string& baseName, const std::string& propName) {
    // Check current namespace
    if (ctx.currentNs && ctx.currentNs->enums.count(baseName)) {
        auto& enumDef = ctx.currentNs->enums[baseName];
        if (enumDef.count(propName)) return ctx.ir_gen->getInt32(enumDef[propName]);
    }
    
    // Legacy fallback
    if (ctx.enums.count(baseName)) {
        auto& enumDef = ctx.enums[baseName];
        if (enumDef.count(propName)) return ctx.ir_gen->getInt32(enumDef[propName]);
    }
    
    LOG_ERROR("Undefined enum variant: " + propName + " on enum " + baseName);
    return ctx.ir_gen->getInt32(0);
}

IRValue* Compiler::resolveProperty(Symbol* baseSym, const std::string& propName) {
    std::string className = baseSym->typeName;
    if ((className.rfind("ptr<", 0) == 0 || className.rfind("ref<", 0) == 0) && className.back() == '>') {
         className = className.substr(4, className.length() - 5);
    }
    
    IRValue* baseAlloca = baseSym->ir_alloca_inst ? baseSym->ir_alloca_inst : ctx.ir_gen->wrap(baseSym->alloca_inst);
    IRType* baseElemTy = baseSym->ir_element_type ? baseSym->ir_element_type : ctx.ir_gen->wrapType(baseSym->element_type);
    
    if (className.find("list<") == 0) {
        if (propName == "len") return ctx.ir_gen->wrap(ListImpl::getSize(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca))));
        if (propName == "empty") {
            IRValue* size = ctx.ir_gen->wrap(ListImpl::getSize(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca))));
            IRValue* cmp = ctx.ir_gen->createICmpEQ(size, ctx.ir_gen->getInt32(0));
            return ctx.ir_gen->wrap(ctx.builder.CreateZExt(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(cmp)), ctx.builder.getInt32Ty(), "empty_zext"));
        }
    } else if (className.find("map<") == 0) {
        llvm::StructType* mapStructTy = static_cast<llvm::StructType*>(ctx.ir_gen->unwrapType(baseElemTy));
        if (propName == "len") return ctx.ir_gen->wrap(MapImpl::getSize(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), mapStructTy));
        if (propName == "empty") {
            IRValue* size = ctx.ir_gen->wrap(MapImpl::getSize(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), mapStructTy));
            IRValue* cmp = ctx.ir_gen->createICmpEQ(size, ctx.ir_gen->getInt32(0));
            return ctx.ir_gen->wrap(ctx.builder.CreateZExt(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(cmp)), ctx.builder.getInt32Ty(), "empty_zext"));
        }
    }
    
    // Check classes (legacy + namespace)
    CompilerContext::ClassInfo* classInfo = nullptr;
    if (ctx.currentNs && ctx.currentNs->classes.count(className)) classInfo = &ctx.currentNs->classes[className];
    else if (ctx.classes.count(className)) classInfo = &ctx.classes[className];
    
    if (classInfo) {
        if (classInfo->members.count(propName)) {
            auto& member = classInfo->members[propName];
            IRType* structTy = ctx.ir_gen->wrapType(classInfo->type);
            IRValue* structPtr = baseAlloca;
            
            if (baseElemTy->isPointer()) {
                structPtr = ctx.ir_gen->createLoad(baseElemTy, baseAlloca, "prop_ptr_base");
            }
            
            IRValue* gep = ctx.ir_gen->createStructGEP(structTy, structPtr, member.index, "prop_ptr");
            IRType* propTy = ctx.ir_gen->wrapType(member.type);
            return ctx.ir_gen->createLoad(propTy, gep, propName + "_val");
        }
    }
    
    LOG_ERROR("Undefined property: " + propName + " on type " + className);
    return ctx.ir_gen->getInt32(0);
}

CompilerContext::FunctionInfo* Compiler::resolveFunction(const std::string& name) {
    if (ctx.currentNs && ctx.currentNs->functions.count(name)) return &ctx.currentNs->functions[name];
    if (ctx.functions.count(name)) return &ctx.functions[name];
    return nullptr;
}

IRValue* Compiler::visitVariable(VariableExpr* var) {
    std::string varName = var->name;
    size_t dotPos = varName.find('.');
    size_t dashPos = varName.find('-');
    size_t sepPos = (dotPos != std::string::npos) ? dotPos : dashPos;
    bool isDash = (dashPos != std::string::npos && dotPos == std::string::npos);
    
    std::string baseName = (sepPos != std::string::npos) ? varName.substr(0, sepPos) : varName;
    std::string propName = (sepPos != std::string::npos) ? varName.substr(sepPos + 1) : "";

    // 1. Unambiguous Enum Check
    if (dotPos != std::string::npos) {
        if ((ctx.currentNs && ctx.currentNs->enums.count(baseName)) || ctx.enums.count(baseName)) {
             return resolveEnum(baseName, propName);
        }
    }

    // 2. Strict Variable Lookup
    Symbol* sym = resolveVariable(baseName);
    if (!sym) {
        if (baseName == "sys" && propName == "env") {
            return ctx.ir_gen->wrap(llvm::ConstantPointerNull::get(ctx.builder.getInt8Ty()->getPointerTo()));
        } else if (baseName == "io" && (propName == "stdout" || propName == "stderr" || propName == "stdin")) {
            int fd = (propName == "stdin") ? 0 : (propName == "stdout" ? 1 : 2);
            std::string fmode = (propName == "stdin") ? "r" : "w";
            llvm::Function* fdopenFunc = ctx.module.getFunction("fdopen");
            if (!fdopenFunc) {
                llvm::FunctionType* fdType = llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), {ctx.builder.getInt32Ty(), ctx.builder.getInt8Ty()->getPointerTo()}, false);
                fdopenFunc = llvm::Function::Create(fdType, llvm::Function::ExternalLinkage, "fdopen", ctx.module);
            }
            llvm::Value* modeStr = ctx.builder.CreateGlobalStringPtr(fmode);
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(fdopenFunc, {ctx.builder.getInt32(fd), modeStr}));
        } else if (ctx.getLLVMType(varName) || varName == "string" || varName == "i32" || varName == "i64" || varName == "u8" || varName == "f32" || varName == "f64") {
            return ctx.ir_gen->getInt32(0); 
        }

        LOG_ERROR("Undefined variable: " + baseName);
        return ctx.ir_gen->getInt32(0); 
    }
    
    IRValue* baseAlloca = sym->ir_alloca_inst ? sym->ir_alloca_inst : ctx.ir_gen->wrap(sym->alloca_inst);
    IRType* baseElemTy = sym->ir_element_type ? sym->ir_element_type : ctx.ir_gen->wrapType(sym->element_type);
    
    if (propName.empty()) {
        return ctx.ir_gen->createLoad(baseElemTy, baseAlloca, baseName + "_val");
    } else {
        llvm::Type* llvmBaseElemTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(baseElemTy));
        if (isDash && (sym->typeName.find("list<") == 0 || llvmBaseElemTy->isArrayTy())) {
            llvm::Value* baseAlloca = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(sym->ir_alloca_inst ? sym->ir_alloca_inst : ctx.ir_gen->wrap(sym->alloca_inst)));
            llvm::Type* elemType = llvmBaseElemTy;
            
            size_t colonPos = propName.find(':');
            if (colonPos != std::string::npos) {
                // Slicing
                if (elemType->isArrayTy()) {
                    LOG_ERROR("Array Slicing not supported on raw arrays yet.");
                    return ctx.ir_gen->getInt32(0);
                }
                
                std::string startStr = propName.substr(0, colonPos);
                std::string endStr = propName.substr(colonPos + 1);
                
                llvm::Value* startVal = startStr.empty() ? ctx.builder.getInt32(0) : ctx.builder.getInt32(std::stoi(startStr));
                llvm::Value* endVal = nullptr;
                if (endStr.empty()) endVal = ListImpl::getSize(ctx, baseAlloca);
                else endVal = ctx.builder.getInt32(std::stoi(endStr));
                
                return ctx.ir_gen->wrap(ListImpl::slice(ctx, baseAlloca, startVal, endVal, elemType));
            } else {
                // Single Element Read
                llvm::Value* idxVal = nullptr;
                try {
                    idxVal = ctx.builder.getInt32(std::stoi(propName));
                } catch(...) {
                    Symbol* idxSym = resolveVariable(propName);
                    if (idxSym) {
                        IRValue* idxAlloca = idxSym->ir_alloca_inst ? idxSym->ir_alloca_inst : ctx.ir_gen->wrap(idxSym->alloca_inst);
                        IRType* idxElemTy = idxSym->ir_element_type ? idxSym->ir_element_type : ctx.ir_gen->wrapType(idxSym->element_type);
                        idxVal = ctx.builder.CreateLoad(static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(idxElemTy)), static_cast<llvm::Value*>(ctx.ir_gen->unwrap(idxAlloca)));
                        if (idxVal->getType()->isIntegerTy(64)) idxVal = ctx.builder.CreateTrunc(idxVal, ctx.builder.getInt32Ty());
                    } else {
                        LOG_ERROR("Invalid list index: " + propName);
                        return ctx.ir_gen->getInt32(0);
                    }
                }
                
                llvm::Value* elemPtr = nullptr;
                if (elemType->isArrayTy()) {
                    elemPtr = ctx.builder.CreateInBoundsGEP(elemType, baseAlloca, {ctx.builder.getInt32(0), idxVal});
                    elemType = elemType->getArrayElementType();
                } else {
                    elemPtr = ListImpl::getElementPtr(ctx, baseAlloca, idxVal, elemType);
                }
                return ctx.ir_gen->wrap(ctx.builder.CreateLoad(elemType, elemPtr, "lst_elem"));
            }
        }
        return resolveProperty(sym, propName);
    }
    return ctx.ir_gen->getInt32(0); 
}

IRValue* Compiler::visitCall(CallExpr* call) {
    std::string funcName = call->functionName;
    size_t dotPos = funcName.find('.');
    
    // Check for lazy-evaluated method arguments before typical eager evaluation
    if (dotPos != std::string::npos) {
        std::string baseName = funcName.substr(0, dotPos);
        std::string methodName = funcName.substr(dotPos + 1);
        
        Symbol* sym = resolveVariable(baseName);
        if (sym) {
            std::string className = sym->typeName;
            if ((className.rfind("ptr<", 0) == 0 || className.rfind("ref<", 0) == 0) && className.back() == '>') {
                 className = className.substr(4, className.length() - 5);
            }
            
            IRValue* baseAlloca = sym->ir_alloca_inst ? sym->ir_alloca_inst : ctx.ir_gen->wrap(sym->alloca_inst);
            IRType* baseElemTy = sym->ir_element_type ? sym->ir_element_type : ctx.ir_gen->wrapType(sym->element_type);
            
            if (className == "buffer") {
                if (methodName == "alloc" && call->args.size() == 1) {
                    IRValue* sizeVal = visitExpression(call->args[0].get());
                    if (sizeVal) BufferImpl::alloc(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), static_cast<llvm::Value*>(ctx.ir_gen->unwrap(sizeVal)));
                    return ctx.ir_gen->getInt32(0);
                } else if (methodName == "write" && call->args.size() >= 2) {
                    IRValue* val = visitExpression(call->args[0].get());
                    std::string typeName = "unknown";
                    if (auto varExpr = dynamic_cast<VariableExpr*>(call->args[1].get())) typeName = varExpr->name;
                    else if (auto litExpr = dynamic_cast<LiteralExpr*>(call->args[1].get())) typeName = litExpr->value;
                    
                    llvm::Type* ty = ctx.getLLVMType(typeName);
                    if (ty && val) BufferImpl::write(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), static_cast<llvm::Value*>(ctx.ir_gen->unwrap(val)), ty);
                    return ctx.ir_gen->getInt32(0);
                } else if (methodName == "read" && call->args.size() >= 1) {
                    std::string typeName = "unknown";
                    if (auto varExpr = dynamic_cast<VariableExpr*>(call->args[0].get())) typeName = varExpr->name;
                    else if (auto litExpr = dynamic_cast<LiteralExpr*>(call->args[0].get())) typeName = litExpr->value;
                    
                    llvm::Type* ty = ctx.getLLVMType(typeName);
                    if (ty) return ctx.ir_gen->wrap(BufferImpl::read(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), ty));
                    return ctx.ir_gen->getInt32(0);
                } else if (methodName == "seek" && call->args.size() >= 1) {
                    IRValue* rawPos = visitExpression(call->args[0].get());
                    llvm::Value* rawVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(rawPos));
                    llvm::Value* pos64 = ctx.builder.CreateZExtOrTrunc(rawVal, ctx.builder.getInt64Ty());
                    BufferImpl::seek(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), pos64, false);
                    BufferImpl::seek(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), pos64, true);
                    return ctx.ir_gen->getInt32(0);
                }
            }
            
            // Eagerly evaluate arguments for everything else
            std::vector<IRValue*> argVals;
            for (auto& arg : call->args) {
                IRValue* val = visitExpression(arg.get());
                if (val) argVals.push_back(val);
                else return ctx.ir_gen->getInt32(0); // Abort if arg evaluation fails
            }
            
            // List Methods
            if (className.find("list<") == 0) {
                 if (methodName == "push" && argVals.size() > 0) {
                     ListImpl::push(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0])));
                     return nullptr;
                 } else if (methodName == "pop") {
                     return ctx.ir_gen->wrap(ListImpl::pop(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(baseElemTy))));
                 } else if (methodName == "get" && argVals.size() > 0) {
                     return ctx.ir_gen->wrap(ListImpl::get(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0])), static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(baseElemTy))));
                 } else if (methodName == "insert" && argVals.size() > 1) {
                     ListImpl::insert(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0])), static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[1])));
                     return nullptr;
                 } else if (methodName == "remove" && argVals.size() > 0) {
                     ListImpl::remove(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0])), static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(baseElemTy)));
                     return nullptr;
                 } else if (methodName == "clear") {
                     ListImpl::clear(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)));
                     return nullptr;
                 } else if (methodName == "len") {
                     return ctx.ir_gen->wrap(ListImpl::getSize(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca))));
                 }
            }
            // Map Methods
            else if (className.find("map<") == 0) {
                 llvm::StructType* mapStructTy = static_cast<llvm::StructType*>(ctx.ir_gen->unwrapType(baseElemTy));
                 if (methodName == "put" && argVals.size() > 1) {
                     MapImpl::put(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0])), static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[1])), mapStructTy);
                     return nullptr;
                 } else if (methodName == "get" && argVals.size() > 0) {
                     return ctx.ir_gen->wrap(MapImpl::get(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0])), static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(baseElemTy)), mapStructTy));
                 } else if (methodName == "remove" && argVals.size() > 0) {
                     MapImpl::remove(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0])), static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(baseElemTy)), mapStructTy);
                     return nullptr;
                 } else if (methodName == "contains" && argVals.size() > 0) {
                     return ctx.ir_gen->wrap(MapImpl::contains(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0])), mapStructTy));
                 } else if (methodName == "len") {
                     return ctx.ir_gen->wrap(MapImpl::getSize(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), mapStructTy));
                 }
            }
            // Process Methods
            else if (className == "process") {
                 llvm::Value* procPtr = ctx.builder.CreateLoad(ctx.builder.getInt8Ty()->getPointerTo(), static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)));
                 if (methodName == "wait") {
                     llvm::Function* waitFunc = ctx.module.getFunction("process_wait");
                     if (!waitFunc) waitFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt32Ty(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "process_wait", ctx.module);
                     return ctx.ir_gen->wrap(ctx.builder.CreateCall(waitFunc, {procPtr}));
                 } else if (methodName == "kill") {
                     llvm::Function* killFunc = ctx.module.getFunction("process_kill");
                     if (!killFunc) killFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "process_kill", ctx.module);
                     ctx.builder.CreateCall(killFunc, {procPtr});
                     return ctx.ir_gen->getInt32(0);
                 }
            }
            // User Class Methods
            else if (!className.empty() && ctx.classes.count(className)) {
                 std::string mangledName = className + "_" + methodName;
                 llvm::Function* func = ctx.module.getFunction(mangledName);
                 if (!func) {
                     LOG_ERROR("Unknown method: " + mangledName);
                     return ctx.ir_gen->getInt32(0);
                 }
                 
                 IRValue* thisPtr = baseAlloca;
                 if (baseElemTy->isPointer()) {
                     thisPtr = ctx.ir_gen->createLoad(baseElemTy, baseAlloca, "this_ptr");
                 }
                 
                 std::vector<IRValue*> methodArgs;
                 methodArgs.push_back(thisPtr);
                 for (auto* a : argVals) methodArgs.push_back(a);
                 
                 return ctx.ir_gen->createCall(mangledName, methodArgs, "call_method");
            }
            
            LOG_ERROR("Unknown method: " + methodName + " on variable " + baseName);
            return ctx.ir_gen->getInt32(0);
        }
        
        // Eagerly evaluate arguments for other functions (sys)
        std::vector<IRValue*> argVals;
        for (auto& arg : call->args) {
            IRValue* val = visitExpression(arg.get());
            if (val) argVals.push_back(val);
            else return ctx.ir_gen->getInt32(0);
        }
        
        // System Namespaces (sys, io, time) - Fallback for now
        if (baseName == "sys" || baseName == "io" || baseName == "time") {
            if (baseName == "sys" && methodName == "exit") {
                llvm::Function* exitFunc = ctx.module.getFunction("exit");
                if (!exitFunc) exitFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {ctx.builder.getInt32Ty()}, false), llvm::Function::ExternalLinkage, "exit", ctx.module);
                llvm::Value* exitCode = argVals.empty() ? ctx.builder.getInt32(0) : static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
                ctx.builder.CreateCall(exitFunc, {exitCode});
                return ctx.ir_gen->getInt32(0);
            } else if (baseName == "sys" && methodName == "get_arg") {
                if (argVals.empty()) return ctx.ir_gen->createGlobalStringPtr("");
                llvm::Value* indexVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
                llvm::GlobalVariable* argvVar = ctx.module.getGlobalVariable("__argv", true);
                llvm::GlobalVariable* argcVar = ctx.module.getGlobalVariable("__argc", true);
                if (!argvVar || !argcVar) return ctx.ir_gen->createGlobalStringPtr("");
                llvm::Value* argvPtr = ctx.builder.CreateLoad(ctx.builder.getInt8Ty()->getPointerTo()->getPointerTo(), argvVar);
                llvm::Value* argcVal = ctx.builder.CreateLoad(ctx.builder.getInt32Ty(), argcVar);
                llvm::Value* argvIsNull = ctx.builder.CreateICmpEQ(argvPtr, llvm::ConstantPointerNull::get(ctx.builder.getInt8Ty()->getPointerTo()->getPointerTo()));
                llvm::Function* parentFunc = ctx.builder.GetInsertBlock()->getParent();
                llvm::BasicBlock* safeBB = llvm::BasicBlock::Create(ctx.context, "arg_safe", parentFunc);
                llvm::BasicBlock* errorBB = llvm::BasicBlock::Create(ctx.context, "arg_error", parentFunc);
                llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(ctx.context, "arg_merge", parentFunc);
                llvm::Value* idxOk = ctx.builder.CreateICmpSLT(indexVal, argcVal);
                llvm::Value* isSafe = ctx.builder.CreateAnd(ctx.builder.CreateNot(argvIsNull), idxOk);
                ctx.builder.CreateCondBr(isSafe, safeBB, errorBB);
                ctx.builder.SetInsertPoint(safeBB);
                llvm::Value* argStrPtr = ctx.builder.CreateInBoundsGEP(ctx.builder.getInt8Ty()->getPointerTo(), argvPtr, indexVal);
                llvm::Value* argStr = ctx.builder.CreateLoad(ctx.builder.getInt8Ty()->getPointerTo(), argStrPtr);
                ctx.builder.CreateBr(mergeBB);
                ctx.builder.SetInsertPoint(errorBB);
                llvm::Value* emptyStr = ctx.builder.CreateGlobalStringPtr("");
                ctx.builder.CreateBr(mergeBB);
                ctx.builder.SetInsertPoint(mergeBB);
                llvm::PHINode* result = ctx.builder.CreatePHI(ctx.builder.getInt8Ty()->getPointerTo(), 2);
                result->addIncoming(argStr, safeBB);
                result->addIncoming(emptyStr, errorBB);
                return ctx.ir_gen->wrap(result);
            } else if (baseName == "sys" && methodName == "env") {
                return ctx.ir_gen->wrap(llvm::ConstantPointerNull::get(ctx.builder.getInt8Ty()->getPointerTo()));
            } else if (baseName == "sys" && methodName == "size") {
                if (argVals.empty()) return ctx.ir_gen->getInt32(0);
                llvm::Type* ty = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(argVals[0]->getType()));
                llvm::DataLayout dl(&ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.getInt64(dl.getTypeAllocSize(ty)));
            } else if (baseName == "sys" && methodName == "exe") {
                if (argVals.empty()) return ctx.ir_gen->wrap(llvm::ConstantPointerNull::get(ctx.builder.getInt8Ty()->getPointerTo()));
                llvm::Value* argvList = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
                if (argvList->getType()->isStructTy()) {
                    llvm::AllocaInst* temp = ctx.builder.CreateAlloca(argvList->getType());
                    ctx.builder.CreateStore(argvList, temp);
                    argvList = ctx.builder.CreateBitCast(temp, ctx.builder.getInt8Ty()->getPointerTo());
                } else if (argvList->getType()->isPointerTy()) {
                    argvList = ctx.builder.CreateBitCast(argvList, ctx.builder.getInt8Ty()->getPointerTo());
                } else return ctx.ir_gen->getInt32(0);
                llvm::Value* cwd = llvm::ConstantPointerNull::get(ctx.builder.getInt8Ty()->getPointerTo());
                llvm::Value* env = llvm::ConstantPointerNull::get(ctx.builder.getInt8Ty()->getPointerTo());
                llvm::Value* shell = ctx.builder.getInt1(false);
                llvm::Value* timeout = ctx.builder.getInt32(-1);
                llvm::Value* capture = ctx.builder.getInt1(false);
                llvm::Function* runtimeFunc = ctx.module.getFunction("sys_exe_runtime");
                if (!runtimeFunc) runtimeFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), {ctx.builder.getInt8Ty()->getPointerTo(), ctx.builder.getInt8Ty()->getPointerTo(), ctx.builder.getInt8Ty()->getPointerTo(), ctx.builder.getInt1Ty(), ctx.builder.getInt32Ty(), ctx.builder.getInt1Ty()}, false), llvm::Function::ExternalLinkage, "sys_exe_runtime", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(runtimeFunc, {argvList, cwd, env, shell, timeout, capture}));
            } else if (baseName == "io" && methodName == "write") {
                if (argVals.size() < 2) return ctx.ir_gen->getInt32(0);
                llvm::Value* handle = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
                llvm::Value* text = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[1]));
                llvm::Function* strlenFunc = ctx.module.getFunction("strlen");
                if (!strlenFunc) strlenFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt64Ty(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "strlen", ctx.module);
                llvm::Value* len = ctx.builder.CreateCall(strlenFunc, {text});
                llvm::Function* fwFunc = ctx.module.getFunction("fwrite");
                if (!fwFunc) fwFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt64Ty(), {ctx.builder.getInt8Ty()->getPointerTo(), ctx.builder.getInt64Ty(), ctx.builder.getInt64Ty(), ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "fwrite", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fwFunc, {text, ctx.builder.getInt64(1), len, handle}));
            } else if (baseName == "io" && methodName == "read") {
                if (argVals.size() < 2) return ctx.ir_gen->wrap(ctx.builder.CreateGlobalStringPtr(""));
                llvm::Value* handle = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
                llvm::Value* size = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[1]));
                if (size->getType()->isIntegerTy(32)) size = ctx.builder.CreateZExt(size, ctx.builder.getInt64Ty());
                llvm::Function* mallocFunc = ctx.module.getFunction("malloc");
                if (!mallocFunc) mallocFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), {ctx.builder.getInt64Ty()}, false), llvm::Function::ExternalLinkage, "malloc", ctx.module);
                llvm::Value* sizePlus1 = ctx.builder.CreateAdd(size, ctx.builder.getInt64(1));
                llvm::Value* buffer = ctx.builder.CreateCall(mallocFunc, {sizePlus1});
                llvm::Function* freadFunc = ctx.module.getFunction("fread");
                if (!freadFunc) freadFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt64Ty(), {ctx.builder.getInt8Ty()->getPointerTo(), ctx.builder.getInt64Ty(), ctx.builder.getInt64Ty(), ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "fread", ctx.module);
                llvm::Value* readBytes = ctx.builder.CreateCall(freadFunc, {buffer, ctx.builder.getInt64(1), size, handle});
                llvm::Value* endPtr = ctx.builder.CreateInBoundsGEP(ctx.builder.getInt8Ty(), buffer, readBytes);
                ctx.builder.CreateStore(ctx.builder.getInt8(0), endPtr);
                return ctx.ir_gen->wrap(buffer);
            } else if (baseName == "io" && methodName == "open") {
                if (argVals.size() < 2) return ctx.ir_gen->wrap(llvm::ConstantPointerNull::get(ctx.builder.getInt8Ty()->getPointerTo()));
                llvm::Value* path = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
                llvm::Value* mode = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[1]));
                llvm::Function* fopenFunc = ctx.module.getFunction("fopen");
                if (!fopenFunc) fopenFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), {ctx.builder.getInt8Ty()->getPointerTo(), ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "fopen", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fopenFunc, {path, mode}));
            } else if (baseName == "io" && methodName == "close") {
                if (argVals.empty()) return ctx.ir_gen->getInt32(0);
                llvm::Function* fcloseFunc = ctx.module.getFunction("fclose");
                if (!fcloseFunc) fcloseFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt32Ty(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "fclose", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(fcloseFunc, {static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]))}));
            } else if (baseName == "io" && methodName == "read_all") {
                if (argVals.empty()) return ctx.ir_gen->wrap(ctx.builder.CreateGlobalStringPtr(""));
                llvm::Value* handle = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
                llvm::Function* fseekFunc = ctx.module.getFunction("fseek");
                if (!fseekFunc) fseekFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt32Ty(), {ctx.builder.getInt8Ty()->getPointerTo(), ctx.builder.getInt64Ty(), ctx.builder.getInt32Ty()}, false), llvm::Function::ExternalLinkage, "fseek", ctx.module);
                llvm::Function* ftellFunc = ctx.module.getFunction("ftell");
                if (!ftellFunc) ftellFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt64Ty(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "ftell", ctx.module);
                llvm::Function* rewindFunc = ctx.module.getFunction("rewind");
                if (!rewindFunc) rewindFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "rewind", ctx.module);
                ctx.builder.CreateCall(fseekFunc, {handle, ctx.builder.getInt64(0), ctx.builder.getInt32(2)});
                llvm::Value* size = ctx.builder.CreateCall(ftellFunc, {handle});
                ctx.builder.CreateCall(rewindFunc, {handle});
                llvm::Function* mallocFunc = ctx.module.getFunction("malloc");
                if (!mallocFunc) mallocFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), {ctx.builder.getInt64Ty()}, false), llvm::Function::ExternalLinkage, "malloc", ctx.module);
                llvm::Value* sizePlus1 = ctx.builder.CreateAdd(size, ctx.builder.getInt64(1));
                llvm::Value* buffer = ctx.builder.CreateCall(mallocFunc, {sizePlus1});
                llvm::Function* freadFunc = ctx.module.getFunction("fread");
                if (!freadFunc) freadFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt64Ty(), {ctx.builder.getInt8Ty()->getPointerTo(), ctx.builder.getInt64Ty(), ctx.builder.getInt64Ty(), ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "fread", ctx.module);
                llvm::Value* readBytes = ctx.builder.CreateCall(freadFunc, {buffer, ctx.builder.getInt64(1), size, handle});
                llvm::Value* endPtr = ctx.builder.CreateInBoundsGEP(ctx.builder.getInt8Ty(), buffer, readBytes);
                ctx.builder.CreateStore(ctx.builder.getInt8(0), endPtr);
                return ctx.ir_gen->wrap(buffer);
            } else if (baseName == "time" && methodName == "now") {
                llvm::Function* timeFunc = ctx.module.getFunction("time_now_ms");
                if (!timeFunc) timeFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt64Ty(), false), llvm::Function::ExternalLinkage, "time_now_ms", ctx.module);
                return ctx.ir_gen->wrap(ctx.builder.CreateCall(timeFunc));
            } else if (baseName == "time" && methodName == "sleep_ms") {
                if (argVals.empty()) return ctx.ir_gen->getInt32(0);
                llvm::Function* sleepFunc = ctx.module.getFunction("time_sleep_ms");
                if (!sleepFunc) sleepFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {ctx.builder.getInt32Ty()}, false), llvm::Function::ExternalLinkage, "time_sleep_ms", ctx.module);
                llvm::Value* msVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
                if (msVal->getType()->isIntegerTy(64)) msVal = ctx.builder.CreateTrunc(msVal, ctx.builder.getInt32Ty());
                ctx.builder.CreateCall(sleepFunc, {msVal});
                return ctx.ir_gen->getInt32(0);
            }
        }
        
        // For standard compilation, we just emit a call to their mangled C names.
        std::string mangledSys = baseName + "_" + methodName;
        if (ctx.module.getFunction(mangledSys)) {
             return ctx.ir_gen->createCall(mangledSys, argVals, "call_sys");
        }
        
        LOG_ERROR("Unknown method or namespace function: " + funcName);
        return ctx.ir_gen->getInt32(0);
    }
    
    // Eagerly evaluate arguments for system functions and standard functions
    std::vector<IRValue*> argVals;
    for (auto& arg : call->args) {
        IRValue* val = visitExpression(arg.get());
        if (val) argVals.push_back(val);
        else return ctx.ir_gen->getInt32(0);
    }

    // System Call Hook (Print)
    if (funcName == "print") {
         // Create or get printf
         std::vector<IRValue*> printArgs;
         IRValue* fmt = ctx.ir_gen->createGlobalStringPtr("%d\n"); // Just a hack, real print needs formatting
         if (!argVals.empty()) {
             llvm::Type* argTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(argVals[0]->getType()));
             if (argTy->isPointerTy()) fmt = ctx.ir_gen->createGlobalStringPtr("%s\n");
         }
         
         printArgs.push_back(fmt);
         if (!argVals.empty()) printArgs.push_back(argVals[0]); 
         
         llvm::Function* printfFunc = ctx.module.getFunction("printf");
         if (!printfFunc) {
             llvm::FunctionType* printfType = llvm::FunctionType::get(ctx.builder.getInt32Ty(), {ctx.builder.getInt8Ty()->getPointerTo()}, true);
             printfFunc = llvm::Function::Create(printfType, llvm::Function::ExternalLinkage, "printf", ctx.module);
         }
         return ctx.ir_gen->createCall("printf", printArgs, "print_call");
    }
    
    // Normal Call
    IRValue* callRes = nullptr;
    CompilerContext::FunctionInfo* funcInfo = resolveFunction(funcName);
    llvm::Function* f = funcInfo ? funcInfo->func : ctx.module.getFunction(funcName);
    
    if (f) {
        for (size_t i = 0; i < argVals.size(); ++i) {
            if (i < f->arg_size()) {
                llvm::Type* expectedTy = f->getFunctionType()->getParamType(i);
                llvm::Value* actualVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[i]));
                llvm::Type* actualTy = actualVal->getType();
                
                if (expectedTy->isPointerTy() && actualTy->isStructTy()) {
                    if (auto varExpr = dynamic_cast<VariableExpr*>(call->args[i].get())) {
                        Symbol* sym = resolveVariable(varExpr->name);
                        if (sym) {
                            actualVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(sym->ir_alloca_inst ? sym->ir_alloca_inst : ctx.ir_gen->wrap(sym->alloca_inst)));
                            argVals[i] = ctx.ir_gen->wrap(actualVal);
                            actualTy = actualVal->getType();
                        }
                    }
                }
                
                if (expectedTy->isPointerTy() && actualTy->isPointerTy() && expectedTy != actualTy) {
                    argVals[i] = ctx.ir_gen->wrap(ctx.builder.CreateBitCast(actualVal, expectedTy));
                }
            }
        }
        callRes = ctx.ir_gen->createCall(f->getName().str(), argVals);
    }
    
    if (!callRes) {
         LOG_ERROR("Unknown function: " + funcName);
         return ctx.ir_gen->getInt32(0);
    }
    return callRes;
}

IRValue* Compiler::visitBinary(BinaryExpr* bin) {
    IRValue* lhs = visitExpression(bin->lhs.get());
    IRValue* rhs = visitExpression(bin->rhs.get());
    if (!lhs || !rhs) return nullptr;
    
    llvm::Type* lTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(lhs->getType()));
    llvm::Type* rTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(rhs->getType()));
    
    if (lTy != rTy) {
        if (lTy->isIntegerTy() && rTy->isIntegerTy()) {
            unsigned lBits = lTy->getIntegerBitWidth();
            unsigned rBits = rTy->getIntegerBitWidth();
            if (lBits < rBits) {
                lhs = ctx.ir_gen->wrap(ctx.builder.CreateSExt(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(lhs)), rTy));
            } else {
                rhs = ctx.ir_gen->wrap(ctx.builder.CreateSExt(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(rhs)), lTy));
            }
        } else if (lTy->isPointerTy() && rTy->isIntegerTy()) {
            rhs = ctx.ir_gen->wrap(ctx.builder.CreateIntToPtr(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(rhs)), lTy));
        } else if (rTy->isPointerTy() && lTy->isIntegerTy()) {
            lhs = ctx.ir_gen->wrap(ctx.builder.CreateIntToPtr(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(lhs)), rTy));
        } else if (lTy->isPointerTy() && rTy->isPointerTy()) {
            rhs = ctx.ir_gen->wrap(ctx.builder.CreateBitCast(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(rhs)), lTy));
        }
    }
    
    if (bin->op == "+") return ctx.ir_gen->createAdd(lhs, rhs);
    if (bin->op == "-") return ctx.ir_gen->createSub(lhs, rhs);
    if (bin->op == "*") return ctx.ir_gen->createMul(lhs, rhs);
    if (bin->op == "/") return ctx.ir_gen->createSDiv(lhs, rhs);
    if (bin->op == "%") return ctx.ir_gen->createSRem(lhs, rhs);
    
    if (bin->op == "eq") return ctx.ir_gen->createICmpEQ(lhs, rhs);
    if (bin->op == "neq") return ctx.ir_gen->createICmpNE(lhs, rhs);
    if (bin->op == "lt") return ctx.ir_gen->createICmpSLT(lhs, rhs);
    if (bin->op == "gt") return ctx.ir_gen->createICmpSGT(lhs, rhs);
    if (bin->op == "le") return ctx.ir_gen->createICmpSLE(lhs, rhs);
    if (bin->op == "ge") return ctx.ir_gen->createICmpSGE(lhs, rhs);
    
    if (bin->op == "and") return ctx.ir_gen->createAnd(lhs, rhs);
    if (bin->op == "or") return ctx.ir_gen->createOr(lhs, rhs);

    LOG_ERROR("Unknown binary operator: " + bin->op);
    return ctx.ir_gen->getInt32(0);
}

} // namespace AST
} // namespace AIML
