#define DEBUG_MODE
#include "ast_compiler.h"
#include "list_impl.h"
#include "map_impl.h"
#include "buffer_impl.h"
#include "string_utils.h"
#include "expression.h"
#include <iostream>

namespace AIML {
namespace AST {

void Compiler::compile(Program* prog) {
    if (!prog) return;
    
    // Create __aiml_init function to hold top-level statements
    IRType* voidTy = ctx.ir_gen->resolveType("void");
    std::vector<IRType*> noArgs;
    IRValue* initFunc = ctx.ir_gen->createFunction("__aiml_init", voidTy, noArgs);
    
    // Create entry block for __aiml_init
    llvm::Function* llvmInitFunc = static_cast<llvm::Function*>(ctx.ir_gen->unwrap(initFunc));
    llvm::BasicBlock* initEntryBB = llvm::BasicBlock::Create(ctx.context, "entry", llvmInitFunc);
    ctx.ir_gen->setInsertPoint(ctx.ir_gen->wrapBlock(initEntryBB));
    
    for (const auto& decl : prog->declarations) {
        visitTopLevel(decl.get());
    }
    
    // Finish __aiml_init
    IRBlock* currentBlock = ctx.ir_gen->getInsertBlock();
    if (!static_cast<llvm::BasicBlock*>(ctx.ir_gen->unwrapBlock(currentBlock))->getTerminator()) {
        ctx.ir_gen->createRet(nullptr);
    }
}

void Compiler::registerTypes(Program* prog) {
    if (!prog) return;

    // Inject BuiltIn Exception Classes
    std::vector<std::string> builtinExceptions = {
        "Exception", "NullPointerException", "TypeMismatchException", "IndexOutOfBoundsException",
        "DivisionByZeroException", "StackOverflowException", "OutOfMemoryException", "TimeoutException",
        "SerializationException", "DeserializationException", "BufferOverflowException", "BufferUnderflowException"
    };

    for (const auto& excName : builtinExceptions) {
        if (!ctx.classes.count(excName)) {
             CompilerContext::ClassInfo classInfo;
             classInfo.name = excName;
             classInfo.version = 0;
             classInfo.parentName = excName == "Exception" ? "" : "Exception";
             
             CompilerContext::StructMember msgMember;
             msgMember.typeName = "string";
             msgMember.type = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType("string")));
             msgMember.index = 0;
             msgMember.access = AccessLevel::Public;
             classInfo.members["message"] = msgMember;
             
             std::vector<llvm::Type*> llvmMemberTypes;
             llvmMemberTypes.push_back(msgMember.type);
             
             llvm::StructType* structType = llvm::StructType::create(ctx.context, excName);
             structType->setBody(llvmMemberTypes);
             classInfo.type = structType;
             
             ctx.classes[excName] = classInfo;
        }
    }

    for (const auto& decl : prog->declarations) {
        registerTypeDecl(decl.get());
    }
}

void Compiler::registerTypeDecl(Node* node) {
    if (auto ns = dynamic_cast<NamespaceDecl*>(node)) {
        ctx.currentNamespace = ns->name;
        LOG_DEBUG("AST Passed Namespace: " + ns->name);
    } else if (auto cfg = dynamic_cast<ConfigDecl*>(node)) {
        if (!cfg->entryPoint.empty()) ctx.config.entryPointName = cfg->entryPoint;
        if (cfg->stackSize) ctx.config.stackSize = cfg->stackSize;
        if (cfg->heapSize) ctx.config.heapSize = cfg->heapSize;
        if (!cfg->target.empty()) ctx.config.target = cfg->target;
        if (!cfg->version.empty()) ctx.config.version = cfg->version;
    } else if (auto imp = dynamic_cast<ImportDecl*>(node)) {
        LOG_DEBUG("AST Processing Import: " + imp->path);
        if (ctx.resolveImportCallback) {
            ctx.resolveImportCallback(imp->path);
        } else {
            LOG_ERROR("resolveImportCallback not bound! Cannot process import: " + imp->path);
        }
    } else if (auto cls = dynamic_cast<ClassDecl*>(node)) {
        std::string className = ctx.qualify(cls->name);
        if (ctx.classes.count(className)) return; // Already registered

        CompilerContext::ClassInfo classInfo;
        classInfo.name = className;
        classInfo.version = cls->version;
        classInfo.parentName = cls->parentName;
        
        std::vector<std::pair<std::string, CompilerContext::StructMember>> orderedMembers;
        int currentIndex = 0;
        
        // 1. Parent
        if (!classInfo.parentName.empty()) {
             if (ctx.classes.count(classInfo.parentName)) {
                 const auto& parentInfo = ctx.classes[classInfo.parentName];
                 for (const auto& [name, member] : parentInfo.members) {
                     orderedMembers.push_back({name, member});
                 }
                 std::sort(orderedMembers.begin(), orderedMembers.end(), [](auto& a, auto& b){ return a.second.index < b.second.index; });
             } else {
                 LOG_ERROR("Unknown parent class: " + classInfo.parentName);
             }
        }
        
        // 1.5 Models (Mixins)
        for (const auto& modelName : cls->models) {
             if (ctx.classes.count(modelName)) {
                 const auto& modelInfo = ctx.classes[modelName];
                 // Models are appended sequentially after parent fields
                 for (auto [name, member] : modelInfo.members) {
                     // Need to re-index so they follow sequentially
                     orderedMembers.push_back({name, member});
                 }
             } else {
                 LOG_ERROR("Unknown model: " + modelName);
             }
        }
        
        currentIndex = orderedMembers.size();
        
        // 2. Class Members (Parsed from AST)
        for (auto& astMember : cls->members) {
            std::string memberName = astMember.name;
            std::string typeName = astMember.type;
            
            bool duplicate = false;
            for(const auto& pair : orderedMembers) if(pair.first == memberName) duplicate = true;
            if(duplicate) {
                LOG_ERROR("Duplicate member '" + memberName + "' in class '" + className + "'");
                continue;
            }
            
            llvm::Type* memberType = ctx.getLLVMType(typeName);
            if (!memberType) {
                LOG_ERROR("Unknown type '" + typeName + "' for member '" + memberName + "'");
                continue; 
            }

            CompilerContext::StructMember member;
            member.index = currentIndex++;
            member.type = memberType;
            member.typeName = typeName;
            member.version = astMember.version;
            member.initValue = std::shared_ptr<Expr>(const_cast<Expr*>(astMember.initialValue.release()));
            
            if (astMember.access == "public") member.access = AccessLevel::Public;
            else if (astMember.access == "private") member.access = AccessLevel::Private;
            else if (astMember.access == "protected") member.access = AccessLevel::Protected;
            
            // Note: Access modifiers, const, and volatile are intentionally lost here 
            // since they weren't fully parsed by the new ClassBuilder yet (it ignored the 3rd/4th elements).
            // Fast follow: we need to parse modifiers in ASTBuilder later.
            
            orderedMembers.push_back({memberName, member});
        }
        
        // Finalize Members
        std::vector<llvm::Type*> llvmMemberTypes;
        for(const auto& [name, member] : orderedMembers) {
            classInfo.members[name] = member;
            llvmMemberTypes.push_back(member.type);
        }

        llvm::StructType* structType = llvm::StructType::create(ctx.context, className);
        structType->setBody(llvmMemberTypes);
        classInfo.type = structType;
        
        ctx.classes[className] = classInfo;
        LOG_DEBUG("AST Registered Class: " + className);
    } else if (auto mod = dynamic_cast<ModelDecl*>(node)) {
        std::string modelName = ctx.qualify(mod->name);
        if (ctx.classes.count(modelName)) return; // Already registered

        CompilerContext::ClassInfo classInfo;
        classInfo.name = modelName;
        classInfo.isModel = true;
        classInfo.version = mod->version;

        std::vector<llvm::Type*> fieldTypes;
        int index = 0;
        
        for (const auto& astMember : mod->fields) {
            std::string fieldName = astMember.name;
            std::string typeName = astMember.type;
            
            llvm::Type* fieldType = ctx.getLLVMType(typeName);
            if (!fieldType) {
                 LOG_ERROR("Unknown type '" + typeName + "' for field '" + fieldName + "' in model '" + modelName + "'");
                 continue; 
            }

            fieldTypes.push_back(fieldType);
            
            CompilerContext::StructMember member;
            member.index = index++;
            member.type = fieldType;
            member.typeName = typeName;
            member.access = AccessLevel::Public; // Models are DTOs
            
            classInfo.members[fieldName] = member;
        }

        llvm::StructType* structType = llvm::StructType::create(ctx.context, modelName);
        structType->setBody(fieldTypes);
        classInfo.type = structType;
        
        ctx.classes[modelName] = classInfo;
        LOG_DEBUG("AST Registered Model: " + modelName);
    } else if (auto enm = dynamic_cast<EnumDecl*>(node)) {
        std::string enumName = ctx.qualify(enm->name);
        std::map<std::string, int> enumValues;
        for (const auto& val : enm->values) {
            enumValues[val.first] = val.second;
        }
        ctx.enums[enumName] = enumValues;
        LOG_DEBUG("AST Registered Enum: " + enumName);
    } else if (auto iface = dynamic_cast<InterfaceDecl*>(node)) {
        
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
        visitStatement(stmt);
    } else if (auto cls = dynamic_cast<ClassDecl*>(node)) {
        for (const auto& method : cls->methods) {
            std::string oldName = method->name;
            method->name = cls->name + "_" + method->name;
            auto it = method->args.insert(method->args.begin(), {"this", "ref<" + cls->name + ">"});
            visitFunction(method.get());
            method->args.erase(it);
            method->name = oldName;
        }
    }
}

void Compiler::visitFunction(FunctionDecl* func) {
    // Basic mapping using our old context structure for now
    std::string mangledName = ctx.qualify(func->name);
    
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
    else if (auto tryStmt = dynamic_cast<TryStmt*>(stmt)) visitTry(tryStmt);
    else if (auto throwStmt = dynamic_cast<ThrowStmt*>(stmt)) visitThrow(throwStmt);
}

void Compiler::visitDeclStmt(DeclStmt* decl) {
    IRType* ty = ctx.ir_gen->resolveType(decl->typeName);
    if (!ty) return;
    
    IRValue* alloca = ctx.ir_gen->createAlloca(ty, decl->varName);
    
    // Zero-initialize the allocated memory
    llvm::Type* unwrappedTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ty));
    llvm::Value* unwrappedAlloca = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(alloca));
    ctx.builder.CreateStore(llvm::Constant::getNullValue(unwrappedTy), unwrappedAlloca);
    
    std::string tName = decl->typeName;
    if (tName.find("list<") == 0 && tName.back() == '>') {
        std::string elemName = tName.substr(5, tName.length() - 6);
        llvm::Type* elemTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(elemName)));
        if (elemTy) ListImpl::initList(ctx, unwrappedAlloca, elemTy, 4);
    } else if (tName.find("map<") == 0 && tName.back() == '>') {
        std::string inner = tName.substr(4, tName.length() - 5);
        size_t colon = inner.find(':');
        if (colon != std::string::npos) {
            std::string kName = inner.substr(0, colon);
            std::string vName = inner.substr(colon + 1);
            llvm::Type* kTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(kName)));
            llvm::Type* vTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(vName)));
            if (kTy && vTy) MapImpl::initMap(ctx, unwrappedAlloca, kTy, vTy, 4);
        }
    }
    
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
    if (targetStr.find("<.") == 0) {
        targetStr = "this." + targetStr.substr(2);
    }
    
    // Support dot-notation (obj.prop) and dash-notation (arr-index)
    size_t dotPos = targetStr.find('.');
    size_t dashPos = targetStr.find('-');
    size_t sepPos = (dotPos != std::string::npos) ? dotPos : dashPos;
    bool isDash = (dashPos != std::string::npos && dotPos == std::string::npos);
    
    std::string baseName = (sepPos != std::string::npos) ? targetStr.substr(0, sepPos) : targetStr;
    std::string propName = (sepPos != std::string::npos) ? targetStr.substr(sepPos + 1) : "";

    Symbol* sym = resolveVariable(baseName);
    if (!sym) {
        LOG_ERROR("Undefined variable (set target): " + targetStr);
        return;
    }
    
    IRValue* baseAlloca = sym->ir_alloca_inst ? sym->ir_alloca_inst : ctx.ir_gen->wrap(sym->alloca_inst);
    IRType* baseElemTy = sym->ir_element_type ? sym->ir_element_type : ctx.ir_gen->wrapType(sym->element_type);
    
    if (!baseAlloca) return;
    
    IRValue* val = visitExpression(setStmt->value.get());
    if (!val) {
        LOG_ERROR("Failed to evaluate value expression for assignment to: " + targetStr);
        return;
    }
    
    if (propName.empty()) {
        ctx.ir_gen->createStore(val, baseAlloca);
    } else {
        std::string className = sym->typeName;
        if ((className.rfind("ptr<", 0) == 0 || className.rfind("ref<", 0) == 0) && className.back() == '>') {
             className = className.substr(4, className.length() - 5);
        }
        
        llvm::Type* llvmBaseElemTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(baseElemTy));
        if (isDash && (className.find("list<") == 0 || llvmBaseElemTy->isArrayTy())) {
            size_t colonPos = propName.find(':');
            if (colonPos != std::string::npos) {
                LOG_ERROR("Slice assignment is not supported yet: " + targetStr);
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
                
                if (member.access == AccessLevel::Private && ctx.currentClassName != className) {
                     LOG_ERROR("Cannot access private member " + propName);
                     return;
                } else if (member.access == AccessLevel::Protected && ctx.currentClassName != className) {
                     LOG_ERROR("Cannot access protected member " + propName);
                     return;
                }
                if (member.is_const) {
                     LOG_ERROR("Cannot assign to const member " + propName);
                     return;
                }
                
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
        LOG_ERROR("Undefined property for set: " + targetStr);
    }
}

void Compiler::visitExprStmt(ExprStmt* exprStmt) {
    visitExpression(exprStmt->expr.get());
}

void Compiler::visitRetStmt(RetStmt* retStmt) {
    llvm::Function* currentFunc = ctx.builder.GetInsertBlock()->getParent();
    llvm::Type* retTy = currentFunc->getReturnType();
    
    if (retStmt->value) {
        IRValue* val = visitExpression(retStmt->value.get());
        if (val) {
            ctx.ir_gen->createRet(val);
        } else {
             LOG_ERROR("Failed to evaluate return expression");
             if (!retTy->isVoidTy()) ctx.ir_gen->createRet(ctx.ir_gen->wrap(llvm::Constant::getNullValue(retTy)));
             else ctx.ir_gen->createRet(nullptr);
        }
    } else {
        if (!retTy->isVoidTy()) {
             LOG_ERROR("Empty return statement in non-void function!");
             ctx.ir_gen->createRet(ctx.ir_gen->wrap(llvm::Constant::getNullValue(retTy)));
        } else {
             ctx.ir_gen->createRet(nullptr);
        }
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

void Compiler::visitTry(TryStmt* tryStmt) {
    LOG_DEBUG("Compiling TryStmt. Has TryBlock: " + std::to_string(tryStmt->tryBlock != nullptr));
    if (!tryStmt->tryBlock) {
        LOG_ERROR("AST Error: Try statement is missing a body. The try block will not execute.");
        return; 
    }

    llvm::Function* func = ctx.builder.GetInsertBlock()->getParent();

    llvm::BasicBlock* tryBlock = llvm::BasicBlock::Create(ctx.context, "try_block", func);
    llvm::BasicBlock* catchBlock = llvm::BasicBlock::Create(ctx.context, "catch_block", func);
    llvm::BasicBlock* contBlock = llvm::BasicBlock::Create(ctx.context, "try_cont", func);

    ctx.builder.CreateBr(tryBlock);
    ctx.builder.SetInsertPoint(tryBlock);

    // Save previous catch block logically if we implement nested tries.
    // For now, assume top-level single try/catch handling via IRGen's context or just emit blocks.
    // To properly support invoke, any call inside tryBlock must be an invoke.
    // Since we don't know which call throws, we need to instruct ir_gen to use invoke instead of call!
    // We'll set a state in ctx to emit invokes to our catch block.
    
    llvm::BasicBlock* oldCatch = ctx.catchBlock;
    ctx.catchBlock = catchBlock;

    if (tryStmt->tryBlock) {
        visitBlockStmt(tryStmt->tryBlock.get());
    }
    
    // Jump to contBlock if we haven't returned/thrown
    if (!ctx.builder.GetInsertBlock()->getTerminator()) {
        ctx.builder.CreateBr(contBlock);
    }

    // Restore catch block
    ctx.catchBlock = oldCatch;

    ctx.builder.SetInsertPoint(catchBlock);
    
    // Emit LandingPad
    llvm::PointerType* ptrTy = llvm::PointerType::getUnqual(ctx.context);
    llvm::StructType* landingPadTy = llvm::StructType::get(ctx.context, {ptrTy, ctx.builder.getInt32Ty()});
    llvm::LandingPadInst* lpad = ctx.builder.CreateLandingPad(landingPadTy, 1, "lpad");
    
    // We only catch C++ exceptions (or our own type) roughly.
    // We need typeinfo of __cxa_exceptio n or equivalent.
    // Null catches all in C++ LLVM IR usually: `catch i8* null`
    lpad->addClause(llvm::ConstantPointerNull::get(ptrTy));
    lpad->setCleanup(false); // We are catching it.
    
    // Extract exception pointer
    llvm::Value* excPtr = ctx.builder.CreateExtractValue(lpad, 0, "exc_ptr");

    // Begin Catch
    llvm::Function* beginCatchFunc = ctx.module.getFunction("__cxa_begin_catch");
    if (!beginCatchFunc) {
        llvm::FunctionType* fty = llvm::FunctionType::get(ptrTy, {ptrTy}, false);
        beginCatchFunc = llvm::Function::Create(fty, llvm::Function::ExternalLinkage, "__cxa_begin_catch", ctx.module);
    }
    
    llvm::Value* rawExc = ctx.builder.CreateCall(beginCatchFunc, {excPtr}, "raw_exc");
    
    // If catch type specified, cast to it. 
    // Wait, AIML dynamic typing or we cast to an Object base?
    // For MVP we just give them the pointer and let them cast, or assume it is an Exception object.
    
    if (tryStmt->catchBlock && !tryStmt->catchVar.empty()) {
        // Assume exception is a ptr to `catchType` struct
        llvm::Type* excType = ctx.getLLVMType(tryStmt->catchType);
        if (!excType) excType = ctx.builder.getInt8Ty(); // fallback
        
        // Since Opaque Pointers (LLVM 18), we don't need BitCast for ptr -> ptr!
        // We just use the ptr rawExc directly, but wrap it in our IRSystem
        llvm::Value* userExc = rawExc;
        
        // Register variable in scope
        IRType* targetTy = ctx.ir_gen->wrapType(ptrTy); // Opaque pointer
        IRValue* alloca = ctx.ir_gen->createAlloca(targetTy, tryStmt->catchVar);
        ctx.builder.CreateStore(userExc, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(alloca)));
        
        Symbol sym;
        sym.ir_alloca_inst = alloca;
        sym.ir_type = ctx.ir_gen->getPointerTy();
        sym.ir_element_type = targetTy;
        sym.typeName = tryStmt->catchType;
        ctx.symbol_table[tryStmt->catchVar] = sym;
        
        visitBlockStmt(tryStmt->catchBlock.get());
    }

    // End Catch
    llvm::Function* endCatchFunc = ctx.module.getFunction("__cxa_end_catch");
    if (!endCatchFunc) {
        llvm::FunctionType* fty = llvm::FunctionType::get(ctx.builder.getVoidTy(), false);
        endCatchFunc = llvm::Function::Create(fty, llvm::Function::ExternalLinkage, "__cxa_end_catch", ctx.module);
    }
    
    if (!ctx.builder.GetInsertBlock()->getTerminator()) {
        ctx.builder.CreateCall(endCatchFunc);
        ctx.builder.CreateBr(contBlock);
    } else {
        // If it terminates, we still need to end catch before returning/throwing?
        // Actually C++ requires invoking end_catch before returning from catch!
        // This is complex for MVP, we'll assume people don't branch directly out of catch without it manually, 
        // or we just put it right before.
    }

    ctx.builder.SetInsertPoint(contBlock);
}

void Compiler::visitThrow(ThrowStmt* throwStmt) {
    if (!throwStmt->value) return;
    
    IRValue* excVal = visitExpression(throwStmt->value.get());
    if (!excVal) return;

    llvm::Value* rawObj = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(excVal));
    llvm::PointerType* ptrTy = llvm::PointerType::getUnqual(ctx.context);

    llvm::Function* allocExcFunc = ctx.module.getFunction("__cxa_allocate_exception");
    if (!allocExcFunc) {
        llvm::FunctionType* fty = llvm::FunctionType::get(ptrTy, {ctx.builder.getInt64Ty()}, false);
        allocExcFunc = llvm::Function::Create(fty, llvm::Function::ExternalLinkage, "__cxa_allocate_exception", ctx.module);
    }

    llvm::Function* throwFunc = ctx.module.getFunction("__cxa_throw");
    if (!throwFunc) {
        llvm::FunctionType* fty = llvm::FunctionType::get(ctx.builder.getVoidTy(), 
            {ptrTy, ptrTy, ptrTy}, false);
        throwFunc = llvm::Function::Create(fty, llvm::Function::ExternalLinkage, "__cxa_throw", ctx.module);
    }

    const llvm::DataLayout& dl = ctx.module.getDataLayout();
    uint64_t excSize = 8; // Pointer size for thrown object reference
    
    llvm::Value* excAlloc = ctx.builder.CreateCall(allocExcFunc, {ctx.builder.getInt64(excSize)}, "exc_alloc");
    
    // cast rawObj to i8* and store it in excAlloc? No, AIML uses objects. We can just store the pointer.
    // With Opaque Pointers, excAlloc is `ptr`. `rawObj` is `ptr`.
    ctx.builder.CreateStore(rawObj, excAlloc);

    llvm::Value* nullTypeInfo = llvm::ConstantPointerNull::get(ptrTy);
    llvm::Value* nullDtor = llvm::ConstantPointerNull::get(ptrTy);

    ctx.builder.CreateCall(throwFunc, {excAlloc, nullTypeInfo, nullDtor});
    ctx.builder.CreateUnreachable();
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
             if (member.initValue != nullptr) {
                  IRValue* fieldInitV = visitExpression(member.initValue.get());
                  if (fieldInitV) {
                      llvm::Value* initVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(fieldInitV));
                      if (initVal) {
                          llvm::Value* rawPtrCast = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(rawPtr));
                          llvm::Value* fieldPtr = ctx.builder.CreateStructGEP(classInfo.type, rawPtrCast, member.index, name + "_gep");
                          ctx.builder.CreateStore(initVal, fieldPtr);
                      } else {
                          LOG_ERROR("Null LLVM value evaluated for default initialization of member: " + name);
                      }
                  } else {
                      LOG_ERROR("Failed to compile AST expression for default initialization of member: " + name);
                  }
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
    
    // Check FQN fallback (e.g. `math.pi` -> `my.math.pi`)
    std::string qualifiedName = ctx.qualify(name);
    if (ctx.symbol_table.count(qualifiedName)) return &ctx.symbol_table[qualifiedName];
    
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
    return nullptr;
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
    return nullptr;
}

CompilerContext::FunctionInfo* Compiler::resolveFunction(const std::string& name) {
    if (ctx.currentNs && ctx.currentNs->functions.count(name)) return &ctx.currentNs->functions[name];
    if (ctx.functions.count(name)) return &ctx.functions[name];
    
    // Check FQN fallback (e.g., calling `calc` inside `my.math` resolves to `my.math.calc`)
    std::string qualifiedName = ctx.qualify(name);
    if (ctx.functions.count(qualifiedName)) return &ctx.functions[qualifiedName];
    
    return nullptr;
}

IRValue* Compiler::visitVariable(VariableExpr* var) {
    std::string varName = var->name;
    if (varName.find("<.") == 0) {
        varName = "this." + varName.substr(2);
    }
    
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
        return nullptr; 
    }
    
    IRValue* baseAlloca = sym->ir_alloca_inst ? sym->ir_alloca_inst : ctx.ir_gen->wrap(sym->alloca_inst);
    IRType* baseElemTy = sym->ir_element_type ? sym->ir_element_type : ctx.ir_gen->wrapType(sym->element_type);
    
    if (propName.empty()) {
        return ctx.ir_gen->createLoad(baseElemTy, baseAlloca, baseName + "_val");
    } else {
        llvm::Type* llvmBaseElemTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(baseElemTy));
        if (isDash) {
            llvm::Value* baseAllocaVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(sym->ir_alloca_inst ? sym->ir_alloca_inst : ctx.ir_gen->wrap(sym->alloca_inst)));
            
            bool isDynamicList = sym->typeName.find("list<") == 0;
            bool isRawArray = llvmBaseElemTy->isArrayTy();
            
            if (isDynamicList || isRawArray) {
                llvm::Type* elemType = isRawArray ? llvmBaseElemTy->getArrayElementType() : nullptr;
                if (isDynamicList) {
                    std::string elemName = sym->typeName.substr(5, sym->typeName.length() - 6);
                    elemType = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(elemName)));
                }

                size_t colonPos = propName.find(':');
                if (colonPos != std::string::npos) {
                    // Slicing
                    if (!isDynamicList) {
                        LOG_ERROR("Array Slicing not supported on raw arrays yet.");
                        return nullptr;
                    }
                    
                    std::string startStr = propName.substr(0, colonPos);
                    std::string endStr = propName.substr(colonPos + 1);
                    
                    llvm::Value* startVal = startStr.empty() ? ctx.builder.getInt32(0) : ctx.builder.getInt32(std::stoi(startStr));
                    llvm::Value* endVal = nullptr;
                    if (endStr.empty()) endVal = ListImpl::getSize(ctx, baseAllocaVal);
                    else endVal = ctx.builder.getInt32(std::stoi(endStr));
                    
                    return ctx.ir_gen->wrap(ListImpl::slice(ctx, baseAllocaVal, startVal, endVal, elemType));
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
                    if (isRawArray) {
                        elemPtr = ctx.builder.CreateInBoundsGEP(llvmBaseElemTy, baseAllocaVal, {ctx.builder.getInt32(0), idxVal});
                    } else {
                        elemPtr = ListImpl::getElementPtr(ctx, baseAllocaVal, idxVal, elemType);
                    }
                    return ctx.ir_gen->wrap(ctx.builder.CreateLoad(elemType, elemPtr, "lst_elem"));
                }
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
                } else if (methodName == "read_versioned" && call->args.size() >= 1) {
                    std::string typeName = "unknown";
                    if (auto varExpr = dynamic_cast<VariableExpr*>(call->args[0].get())) typeName = varExpr->name;
                    else if (auto litExpr = dynamic_cast<LiteralExpr*>(call->args[0].get())) typeName = litExpr->value;
                    
                    llvm::Type* ty = ctx.getLLVMType(typeName);
                    if (ty) {
                        auto realEval = [this](CompilerContext& c, std::shared_ptr<AIML::AST::Expr> e) -> llvm::Value* { 
                             IRValue* v = this->visitExpression(e.get());
                             if (!v) return llvm::Constant::getNullValue(ctx.getLLVMType("buffer")); // or better err
                             return static_cast<llvm::Value*>(c.ir_gen->unwrap(v));
                        };
                        llvm::Value* explicitVer = nullptr;
                        if (call->args.size() >= 2) {
                            IRValue* verVal = visitExpression(call->args[1].get());
                            if (verVal) explicitVer = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(verVal));
                        }
                        return ctx.ir_gen->wrap(BufferImpl::read_versioned(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), ty, realEval, explicitVer));
                    }
                    return ctx.ir_gen->getInt32(0);
                }
            } else if (className == "string" && methodName == "len") {
                  llvm::Function* strlenFunc = ctx.module.getFunction("strlen");
                  if (!strlenFunc) strlenFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt64Ty(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "strlen", ctx.module);
                  llvm::Value* strPtr = ctx.builder.CreateLoad(ctx.builder.getInt8Ty()->getPointerTo(), static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)));
                  llvm::Value* len64 = ctx.builder.CreateCall(strlenFunc, {strPtr});
                  llvm::Value* len32 = ctx.builder.CreateTrunc(len64, ctx.builder.getInt32Ty());
                  return ctx.ir_gen->wrap(len32);
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
                 
                 llvm::Type* vTy = nullptr;
                 std::string inner = className.substr(4, className.length() - 5);
                 size_t colon = inner.find(':');
                 if (colon != std::string::npos) {
                     std::string vName = inner.substr(colon + 1);
                     vTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(ctx.ir_gen->resolveType(vName)));
                 }
                 if (!vTy) vTy = ctx.builder.getInt8Ty()->getPointerTo(); // Fallback string

                 if (methodName == "put" && argVals.size() > 1) {
                     llvm::Value* keyVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
                     llvm::Value* valVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[1]));
                     
                     if (vTy->isPointerTy() && valVal->getType()->isStructTy()) {
                         if (auto varExpr = dynamic_cast<VariableExpr*>(call->args[1].get())) {
                             Symbol* sym = resolveVariable(varExpr->name);
                             if (sym) valVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(sym->ir_alloca_inst ? sym->ir_alloca_inst : ctx.ir_gen->wrap(sym->alloca_inst)));
                         }
                     }
                     if (vTy->isPointerTy() && valVal->getType()->isPointerTy() && valVal->getType() != vTy) {
                         valVal = ctx.builder.CreateBitCast(valVal, vTy);
                     }
                     if (keyVal->getType()->isPointerTy() && keyVal->getType() != ctx.builder.getInt8Ty()->getPointerTo()) {
                         keyVal = ctx.builder.CreateBitCast(keyVal, ctx.builder.getInt8Ty()->getPointerTo());
                     }
                     
                     MapImpl::put(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), keyVal, valVal, mapStructTy);
                     return nullptr;
                 } else if (methodName == "get" && argVals.size() > 0) {
                     llvm::Value* keyVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
                     if (keyVal->getType()->isPointerTy() && keyVal->getType() != ctx.builder.getInt8Ty()->getPointerTo()) {
                         keyVal = ctx.builder.CreateBitCast(keyVal, ctx.builder.getInt8Ty()->getPointerTo());
                     }
                     return ctx.ir_gen->wrap(MapImpl::get(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), keyVal, vTy, mapStructTy));
                 } else if (methodName == "remove" && argVals.size() > 0) {
                     llvm::Value* keyVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
                     if (keyVal->getType()->isPointerTy() && keyVal->getType() != ctx.builder.getInt8Ty()->getPointerTo()) {
                         keyVal = ctx.builder.CreateBitCast(keyVal, ctx.builder.getInt8Ty()->getPointerTo());
                     }
                     MapImpl::remove(ctx, static_cast<llvm::Value*>(ctx.ir_gen->unwrap(baseAlloca)), keyVal, vTy, mapStructTy);
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
                     std::string currClass = className;
                     while (!currClass.empty() && ctx.classes.count(currClass)) {
                         std::string pName = ctx.classes[currClass].parentName;
                         if (!pName.empty()) {
                             currClass = pName;
                             func = ctx.module.getFunction(currClass + "_" + methodName);
                             if (func) break;
                         } else {
                             break;
                         }
                     }
                 }
                 if (!func) {
                     LOG_ERROR("Unknown method: " + className + "_" + methodName);
                     return ctx.ir_gen->getInt32(0);
                 }
                 
                 IRValue* thisPtr = baseAlloca;
                 if (baseElemTy->isPointer()) {
                     thisPtr = ctx.ir_gen->createLoad(baseElemTy, baseAlloca, "this_ptr");
                 }
                 if (!thisPtr) return ctx.ir_gen->getInt32(0);
                 
                 llvm::Type* expectedThisTy = func->getFunctionType()->getParamType(0);
                 llvm::Value* rawThis = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(thisPtr));
                 if (rawThis->getType() != expectedThisTy) {
                     rawThis = ctx.builder.CreateBitCast(rawThis, expectedThisTy, "this_cast");
                     thisPtr = ctx.ir_gen->wrap(rawThis);
                 }
                 
                 std::vector<IRValue*> methodArgs;
                 methodArgs.push_back(thisPtr);
                 for (auto* a : argVals) methodArgs.push_back(a);
                 
                 return ctx.ir_gen->createCall(func->getName().str(), methodArgs, "call_method");
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
                llvm::Type* strTy = ctx.builder.getInt8Ty()->getPointerTo();
                llvm::StructType* mapTy = llvm::StructType::get(ctx.context, {
                    ctx.builder.getInt32Ty(),
                    ctx.builder.getInt32Ty(),
                    ctx.builder.getInt8Ty()->getPointerTo(),
                    strTy->getPointerTo(),
                    strTy->getPointerTo()
                });
                llvm::DataLayout dl(&ctx.module);
                uint64_t mapSize = dl.getTypeAllocSize(mapTy);
                llvm::Function* callocFunc = ctx.module.getFunction("calloc");
                if (!callocFunc) callocFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), {ctx.builder.getInt64Ty(), ctx.builder.getInt64Ty()}, false), llvm::Function::ExternalLinkage, "calloc", ctx.module);
                llvm::Value* mapVoidPtr = ctx.builder.CreateCall(callocFunc, {ctx.builder.getInt64(1), ctx.builder.getInt64(mapSize)});
                llvm::Value* mapPtr = ctx.builder.CreateBitCast(mapVoidPtr, mapTy->getPointerTo(), "sys_env_map");
                MapImpl::initMap(ctx, mapPtr, strTy, strTy, 4);
                return ctx.ir_gen->wrap(mapPtr);
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
                
                llvm::Function* parentFunc = ctx.builder.GetInsertBlock()->getParent();
                llvm::BasicBlock* validHandleBB = llvm::BasicBlock::Create(ctx.context, "valid_write_handle", parentFunc);
                llvm::BasicBlock* invalidHandleBB = llvm::BasicBlock::Create(ctx.context, "invalid_write_handle", parentFunc);
                llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(ctx.context, "merge_write", parentFunc);
                
                llvm::Value* isNull = ctx.builder.CreateICmpEQ(handle, llvm::ConstantPointerNull::get(ctx.builder.getInt8Ty()->getPointerTo()));
                ctx.builder.CreateCondBr(isNull, invalidHandleBB, validHandleBB);
                
                // Invalid Handle
                ctx.builder.SetInsertPoint(invalidHandleBB);
                ctx.builder.CreateBr(mergeBB);
                
                // Valid Handle
                ctx.builder.SetInsertPoint(validHandleBB);
                llvm::Function* strlenFunc = ctx.module.getFunction("strlen");
                if (!strlenFunc) strlenFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt64Ty(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "strlen", ctx.module);
                llvm::Value* len = ctx.builder.CreateCall(strlenFunc, {text});
                llvm::Function* fwFunc = ctx.module.getFunction("fwrite");
                if (!fwFunc) fwFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt64Ty(), {ctx.builder.getInt8Ty()->getPointerTo(), ctx.builder.getInt64Ty(), ctx.builder.getInt64Ty(), ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "fwrite", ctx.module);
                llvm::Value* writeBytes = ctx.builder.CreateCall(fwFunc, {text, ctx.builder.getInt64(1), len, handle});
                ctx.builder.CreateBr(mergeBB);
                
                // Merge
                ctx.builder.SetInsertPoint(mergeBB);
                llvm::PHINode* phi = ctx.builder.CreatePHI(ctx.builder.getInt64Ty(), 2, "write_res");
                phi->addIncoming(ctx.builder.getInt64(0), invalidHandleBB);
                phi->addIncoming(writeBytes, validHandleBB);
                
                // fwrite returns size_t, we return the i32 casted value wrapped.
                return ctx.ir_gen->wrap(ctx.builder.CreateTrunc(phi, ctx.builder.getInt32Ty()));
            } else if (baseName == "io" && methodName == "read") {
                if (argVals.size() < 2) return ctx.ir_gen->wrap(ctx.builder.CreateGlobalStringPtr(""));
                llvm::Value* handle = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
                
                llvm::Function* parentFunc = ctx.builder.GetInsertBlock()->getParent();
                llvm::BasicBlock* validHandleBB = llvm::BasicBlock::Create(ctx.context, "valid_handle", parentFunc);
                llvm::BasicBlock* invalidHandleBB = llvm::BasicBlock::Create(ctx.context, "invalid_handle", parentFunc);
                llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(ctx.context, "merge_read", parentFunc);
                
                llvm::Value* isNull = ctx.builder.CreateICmpEQ(handle, llvm::ConstantPointerNull::get(ctx.builder.getInt8Ty()->getPointerTo()));
                ctx.builder.CreateCondBr(isNull, invalidHandleBB, validHandleBB);
                
                // Invalid Handle
                ctx.builder.SetInsertPoint(invalidHandleBB);
                llvm::Value* emptyStr = ctx.builder.CreateGlobalStringPtr("");
                ctx.builder.CreateBr(mergeBB);
                
                // Valid Handle
                ctx.builder.SetInsertPoint(validHandleBB);
                llvm::Value* size = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[1]));
                if (size->getType()->isIntegerTy(32)) size = ctx.builder.CreateZExt(size, ctx.builder.getInt64Ty());
                
                // Check if size > 0
                llvm::BasicBlock* doReadBB = llvm::BasicBlock::Create(ctx.context, "do_read", parentFunc);
                llvm::Value* isValidSize = ctx.builder.CreateICmpSGT(size, ctx.builder.getInt64(0));
                ctx.builder.CreateCondBr(isValidSize, doReadBB, invalidHandleBB);
                
                ctx.builder.SetInsertPoint(doReadBB);
                llvm::Function* mallocFunc = ctx.module.getFunction("malloc");
                if (!mallocFunc) mallocFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), {ctx.builder.getInt64Ty()}, false), llvm::Function::ExternalLinkage, "malloc", ctx.module);
                llvm::Value* sizePlus1 = ctx.builder.CreateAdd(size, ctx.builder.getInt64(1));
                llvm::Value* buffer = ctx.builder.CreateCall(mallocFunc, {sizePlus1});
                llvm::Function* freadFunc = ctx.module.getFunction("fread");
                if (!freadFunc) freadFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt64Ty(), {ctx.builder.getInt8Ty()->getPointerTo(), ctx.builder.getInt64Ty(), ctx.builder.getInt64Ty(), ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "fread", ctx.module);
                llvm::Value* readBytes = ctx.builder.CreateCall(freadFunc, {buffer, ctx.builder.getInt64(1), size, handle});
                llvm::Value* endPtr = ctx.builder.CreateInBoundsGEP(ctx.builder.getInt8Ty(), buffer, readBytes);
                ctx.builder.CreateStore(ctx.builder.getInt8(0), endPtr);
                ctx.builder.CreateBr(mergeBB);
                
                // Merge
                ctx.builder.SetInsertPoint(mergeBB);
                llvm::PHINode* phi = ctx.builder.CreatePHI(ctx.builder.getInt8Ty()->getPointerTo(), 2, "read_res");
                phi->addIncoming(emptyStr, invalidHandleBB);
                phi->addIncoming(buffer, doReadBB);
                
                return ctx.ir_gen->wrap(phi);
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
                
                llvm::Function* parentFunc = ctx.builder.GetInsertBlock()->getParent();
                llvm::BasicBlock* validHandleBB = llvm::BasicBlock::Create(ctx.context, "valid_handle", parentFunc);
                llvm::BasicBlock* invalidHandleBB = llvm::BasicBlock::Create(ctx.context, "invalid_handle", parentFunc);
                llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(ctx.context, "merge_read_all", parentFunc);
                
                llvm::Value* isNull = ctx.builder.CreateICmpEQ(handle, llvm::ConstantPointerNull::get(ctx.builder.getInt8Ty()->getPointerTo()));
                ctx.builder.CreateCondBr(isNull, invalidHandleBB, validHandleBB);
                
                // Invalid Handle
                ctx.builder.SetInsertPoint(invalidHandleBB);
                llvm::Value* emptyStr = ctx.builder.CreateGlobalStringPtr("");
                ctx.builder.CreateBr(mergeBB);
                
                // Valid Handle
                ctx.builder.SetInsertPoint(validHandleBB);
                llvm::Function* fseekFunc = ctx.module.getFunction("fseek");
                if (!fseekFunc) fseekFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt32Ty(), {ctx.builder.getInt8Ty()->getPointerTo(), ctx.builder.getInt64Ty(), ctx.builder.getInt32Ty()}, false), llvm::Function::ExternalLinkage, "fseek", ctx.module);
                llvm::Function* ftellFunc = ctx.module.getFunction("ftell");
                if (!ftellFunc) ftellFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt64Ty(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "ftell", ctx.module);
                llvm::Function* rewindFunc = ctx.module.getFunction("rewind");
                if (!rewindFunc) rewindFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "rewind", ctx.module);
                ctx.builder.CreateCall(fseekFunc, {handle, ctx.builder.getInt64(0), ctx.builder.getInt32(2)});
                llvm::Value* size = ctx.builder.CreateCall(ftellFunc, {handle});
                ctx.builder.CreateCall(rewindFunc, {handle});
                
                // Check if size > 0
                llvm::BasicBlock* doReadBB = llvm::BasicBlock::Create(ctx.context, "do_read_all", parentFunc);
                llvm::Value* isValidSize = ctx.builder.CreateICmpSGT(size, ctx.builder.getInt64(0));
                ctx.builder.CreateCondBr(isValidSize, doReadBB, invalidHandleBB);
                
                ctx.builder.SetInsertPoint(doReadBB);
                llvm::Function* mallocFunc = ctx.module.getFunction("malloc");
                if (!mallocFunc) mallocFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt8Ty()->getPointerTo(), {ctx.builder.getInt64Ty()}, false), llvm::Function::ExternalLinkage, "malloc", ctx.module);
                llvm::Value* sizePlus1 = ctx.builder.CreateAdd(size, ctx.builder.getInt64(1));
                llvm::Value* buffer = ctx.builder.CreateCall(mallocFunc, {sizePlus1});
                llvm::Function* freadFunc = ctx.module.getFunction("fread");
                if (!freadFunc) freadFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt64Ty(), {ctx.builder.getInt8Ty()->getPointerTo(), ctx.builder.getInt64Ty(), ctx.builder.getInt64Ty(), ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "fread", ctx.module);
                llvm::Value* readBytes = ctx.builder.CreateCall(freadFunc, {buffer, ctx.builder.getInt64(1), size, handle});
                llvm::Value* endPtr = ctx.builder.CreateInBoundsGEP(ctx.builder.getInt8Ty(), buffer, readBytes);
                ctx.builder.CreateStore(ctx.builder.getInt8(0), endPtr);
                ctx.builder.CreateBr(mergeBB);
                
                // Merge
                ctx.builder.SetInsertPoint(mergeBB);
                llvm::PHINode* phi = ctx.builder.CreatePHI(ctx.builder.getInt8Ty()->getPointerTo(), 2, "read_all_res");
                phi->addIncoming(emptyStr, invalidHandleBB);
                phi->addIncoming(buffer, doReadBB);
                
                return ctx.ir_gen->wrap(phi);
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
        
        // FQN Fallback (e.g. my.math.add)
        CompilerContext::FunctionInfo* fInfo = resolveFunction(funcName);
        llvm::Function* f = fInfo ? fInfo->func : ctx.module.getFunction(funcName);
        if (f) {
            std::vector<llvm::Value*> finalArgs;
            for (size_t i = 0; i < argVals.size(); ++i) {
                 finalArgs.push_back(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[i])));
            }
            return ctx.ir_gen->wrap(ctx.builder.CreateCall(f, finalArgs));
        }
        
        LOG_ERROR("Unknown method or namespace function: " + funcName);
        return ctx.ir_gen->getInt32(0);
    }
    
    // Eagerly evaluate arguments for system functions and standard functions
    std::vector<IRValue*> argVals;
    for (size_t i = 0; i < call->args.size(); ++i) {
        IRValue* val = visitExpression(call->args[i].get());
        if (val) {
            argVals.push_back(val);
        } else {
            LOG_ERROR("Failed to evaluate argument " + std::to_string(i) + " for call to: " + funcName);
            return ctx.ir_gen->getInt32(0);
        }
    }

    // System Call Hook (Print)
    if (funcName == "print") {
         std::vector<IRValue*> printArgs;
         std::string formatStr = "";
         
         // Build format string and arguments list
         for (size_t i = 0; i < argVals.size(); ++i) {
             IRValue* argVal = argVals[i];
             llvm::Type* argTy = static_cast<llvm::Type*>(ctx.ir_gen->unwrapType(argVal->getType()));
             
             if (argTy->isPointerTy()) {
                 formatStr += "%s";
                 
                 // Null pointer guard
                 llvm::Value* rawVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVal));
                 llvm::Value* isNull = ctx.builder.CreateIsNull(rawVal, "print_null_check");
                 llvm::Value* emptyStr = ctx.builder.CreateGlobalStringPtr("(null)", "print_null_str");
                 llvm::Value* safeStr = ctx.builder.CreateSelect(isNull, emptyStr, rawVal, "print_safe_str");
                 printArgs.push_back(ctx.ir_gen->wrap(safeStr));
             } else {
                 formatStr += "%d";
                 printArgs.push_back(argVal);
             }
         }
         formatStr += "\n";
         
         // Insert format string as first argument
         IRValue* fmt = ctx.ir_gen->createGlobalStringPtr(formatStr);
         printArgs.insert(printArgs.begin(), fmt);
         
         llvm::Function* printfFunc = ctx.module.getFunction("printf");
         if (!printfFunc) {
             llvm::FunctionType* printfType = llvm::FunctionType::get(ctx.builder.getInt32Ty(), {ctx.builder.getInt8Ty()->getPointerTo()}, true);
             printfFunc = llvm::Function::Create(printfType, llvm::Function::ExternalLinkage, "printf", ctx.module);
         }
         return ctx.ir_gen->createCall("printf", printArgs, "print_call");
    } else if (funcName == "free") {
         llvm::Function* freeFunc = ctx.module.getFunction("free");
         if (!freeFunc) freeFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getVoidTy(), {ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "free", ctx.module);
         if (argVals.empty()) return ctx.ir_gen->getInt32(0);
         llvm::Value* ptrVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[0]));
         if (ptrVal->getType() != ctx.builder.getInt8Ty()->getPointerTo()) {
             ptrVal = ctx.builder.CreateBitCast(ptrVal, ctx.builder.getInt8Ty()->getPointerTo());
         }
         ctx.builder.CreateCall(freeFunc, {ptrVal});
         return ctx.ir_gen->getInt32(0);
    }
    
    // Normal Call
    IRValue* callRes = nullptr;
    CompilerContext::FunctionInfo* funcInfo = resolveFunction(funcName);
    llvm::Function* f = funcInfo ? funcInfo->func : ctx.module.getFunction(funcName);
    
    if (f) {
        if (!f->isVarArg() && argVals.size() < f->arg_size()) {
            LOG_ERROR("Not enough arguments for function " + funcName + ". Expected " + std::to_string(f->arg_size()) + ", got " + std::to_string(argVals.size()));
        }
        for (size_t i = 0; i < argVals.size(); ++i) {
            if (i < f->arg_size()) {
                llvm::Type* expectedTy = f->getFunctionType()->getParamType(i);
                llvm::Value* actualVal = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(argVals[i]));
                if (!actualVal) {
                    LOG_ERROR("Null LLVM value passed as argument " + std::to_string(i) + " for function " + funcName);
                    continue;
                }
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
        if (ctx.catchBlock) {
             llvm::Function* currentFunc = ctx.builder.GetInsertBlock()->getParent();
             llvm::BasicBlock* invokeCont = llvm::BasicBlock::Create(ctx.context, "invoke_cont", currentFunc);
             
             std::vector<llvm::Value*> rawArgs;
             for (auto arg : argVals) rawArgs.push_back(static_cast<llvm::Value*>(ctx.ir_gen->unwrap(arg)));
             
             llvm::Value* invRes = ctx.builder.CreateInvoke(f, invokeCont, ctx.catchBlock, rawArgs, funcName + "_inv");
             callRes = ctx.ir_gen->wrap(invRes);
             ctx.builder.SetInsertPoint(invokeCont);
        } else {
             callRes = ctx.ir_gen->createCall(f->getName().str(), argVals);
        }
    }
    
    if (!callRes) {
         LOG_ERROR("Unknown function: " + funcName);
         return nullptr;
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
    
    if (bin->op == "eq") {
        if (lTy->isPointerTy() && rTy->isPointerTy()) {
            llvm::Function* parentFunc = ctx.builder.GetInsertBlock()->getParent();
            llvm::BasicBlock* checkNullBB = llvm::BasicBlock::Create(ctx.context, "cmp_check_null", parentFunc);
            llvm::BasicBlock* doStrcmpBB = llvm::BasicBlock::Create(ctx.context, "cmp_do_strcmp", parentFunc);
            llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(ctx.context, "cmp_merge", parentFunc);
            
            llvm::Value* rawLhs = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(lhs));
            llvm::Value* rawRhs = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(rhs));
            
            llvm::Value* isLhsNull = ctx.builder.CreateIsNull(rawLhs);
            llvm::Value* isRhsNull = ctx.builder.CreateIsNull(rawRhs);
            
            // If both null -> true (eq), if one null -> false (neq)
            llvm::Value* ptrEq = ctx.builder.CreateICmpEQ(rawLhs, rawRhs);
            llvm::Value* anyNull = ctx.builder.CreateOr(isLhsNull, isRhsNull);
            
            ctx.builder.CreateCondBr(anyNull, checkNullBB, doStrcmpBB);
            
            // Check null case (fast path or actually null ptr)
            ctx.builder.SetInsertPoint(checkNullBB);
            ctx.builder.CreateBr(mergeBB);
            
            // Do Strcmp
            ctx.builder.SetInsertPoint(doStrcmpBB);
            llvm::Function* strcmpFunc = ctx.module.getFunction("strcmp");
            if (!strcmpFunc) strcmpFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt32Ty(), {ctx.builder.getInt8Ty()->getPointerTo(), ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "strcmp", ctx.module);
            llvm::Value* cmpRes = ctx.builder.CreateCall(strcmpFunc, {rawLhs, rawRhs});
            llvm::Value* strEq = ctx.builder.CreateICmpEQ(cmpRes, ctx.builder.getInt32(0));
            ctx.builder.CreateBr(mergeBB);
            
            // Merge
            ctx.builder.SetInsertPoint(mergeBB);
            llvm::PHINode* resultNode = ctx.builder.CreatePHI(ctx.builder.getInt1Ty(), 2, "streq_res");
            resultNode->addIncoming(ptrEq, checkNullBB);
            resultNode->addIncoming(strEq, doStrcmpBB);
            
            return ctx.ir_gen->wrap(resultNode);
        }
        return ctx.ir_gen->createICmpEQ(lhs, rhs);
    }
    if (bin->op == "neq") {
        if (lTy->isPointerTy() && rTy->isPointerTy()) {
            // Re-use logic above but invert result
            llvm::Function* parentFunc = ctx.builder.GetInsertBlock()->getParent();
            llvm::BasicBlock* checkNullBB = llvm::BasicBlock::Create(ctx.context, "cmp_check_null_ne", parentFunc);
            llvm::BasicBlock* doStrcmpBB = llvm::BasicBlock::Create(ctx.context, "cmp_do_strcmp_ne", parentFunc);
            llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(ctx.context, "cmp_merge_ne", parentFunc);
            
            llvm::Value* rawLhs = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(lhs));
            llvm::Value* rawRhs = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(rhs));
            
            llvm::Value* isLhsNull = ctx.builder.CreateIsNull(rawLhs);
            llvm::Value* isRhsNull = ctx.builder.CreateIsNull(rawRhs);
            
            llvm::Value* ptrEq = ctx.builder.CreateICmpEQ(rawLhs, rawRhs);
            llvm::Value* ptrNeq = ctx.builder.CreateNot(ptrEq);
            llvm::Value* anyNull = ctx.builder.CreateOr(isLhsNull, isRhsNull);
            
            ctx.builder.CreateCondBr(anyNull, checkNullBB, doStrcmpBB);
            
            ctx.builder.SetInsertPoint(checkNullBB);
            ctx.builder.CreateBr(mergeBB);
            
            ctx.builder.SetInsertPoint(doStrcmpBB);
            llvm::Function* strcmpFunc = ctx.module.getFunction("strcmp");
            if (!strcmpFunc) strcmpFunc = llvm::Function::Create(llvm::FunctionType::get(ctx.builder.getInt32Ty(), {ctx.builder.getInt8Ty()->getPointerTo(), ctx.builder.getInt8Ty()->getPointerTo()}, false), llvm::Function::ExternalLinkage, "strcmp", ctx.module);
            llvm::Value* cmpRes = ctx.builder.CreateCall(strcmpFunc, {rawLhs, rawRhs});
            llvm::Value* strNeq = ctx.builder.CreateICmpNE(cmpRes, ctx.builder.getInt32(0));
            ctx.builder.CreateBr(mergeBB);
            
            ctx.builder.SetInsertPoint(mergeBB);
            llvm::PHINode* resultNode = ctx.builder.CreatePHI(ctx.builder.getInt1Ty(), 2, "strneq_res");
            resultNode->addIncoming(ptrNeq, checkNullBB);
            resultNode->addIncoming(strNeq, doStrcmpBB);
            
            return ctx.ir_gen->wrap(resultNode);
        }
        return ctx.ir_gen->createICmpNE(lhs, rhs);
    }
    if (bin->op == "lt") return ctx.ir_gen->createICmpSLT(lhs, rhs);
    if (bin->op == "gt") return ctx.ir_gen->createICmpSGT(lhs, rhs);
    if (bin->op == "le") return ctx.ir_gen->createICmpSLE(lhs, rhs);
    if (bin->op == "ge") return ctx.ir_gen->createICmpSGE(lhs, rhs);
    
    if (bin->op == "and") return ctx.ir_gen->createAnd(lhs, rhs);
    if (bin->op == "or") return ctx.ir_gen->createOr(lhs, rhs);

    LOG_ERROR("Unknown binary operator: " + bin->op);
    return nullptr;
}

} // namespace AST
} // namespace AIML
