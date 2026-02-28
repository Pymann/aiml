#define DEBUG_MODE
#include "ast_compiler.h"
#include "builtin_registry.h"
#include "fptable.h"
#include "schema_registry.h"
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

    registerBuiltins(ctx);

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

    // Emit hot-reload support tables (after all functions and types are registered)
    emitFptable(ctx);
    emitSchemaRegistry(ctx);
}
void Compiler::registerTypes(Program* prog) {
    if (!prog) return;

    // Inject BuiltIn Exception Classes
    std::vector<std::string> builtinExceptions = {
        "Exception", "NullPointerException", "TypeMismatchException", "IndexOutOfBoundsException",
        "DivisionByZeroException", "StackOverflowException", "OutOfMemoryException", "TimeoutException",
        "SerializationException", "DeserializationException", "BufferOverflowException", "BufferUnderflowException",
        "InvalidThreadStateException"
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
        classInfo.compiled_version = cls->version;  // locked at compile time
        classInfo.parentName = cls->parentName;
        classInfo.sourceYaml = YAML::Dump(cls->sourceNode);
        
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
            
            member.is_const = astMember.is_const;
            #ifdef LLVM 18
            // member.is_volatile not supported structurally in opaque pointers.
            // Requires attaching volatile flag to the underlying Load/Store instructions.
            #endif
            
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

        // Compute packed_size / padded_size for hot-reload calloc padding
        {
            int packed = (int)ctx.module.getDataLayout().getTypeAllocSize(structType);
            classInfo.packed_size = packed;
            int padded = 1;
            while (padded < packed) padded <<= 1;
            classInfo.padded_size = padded;
        }

        // Track user-defined types for sys.bundle dependency closure.
        // collectUserTypes recurses into list<T> / map<K,V> to find inner types.
        auto track = [&](const std::string& typeName) {
            std::set<std::string> found;
            ctx.collectUserTypes(typeName, found);
            classInfo.referencedTypes.insert(found.begin(), found.end());
        };
        for (const auto& [mname, member] : classInfo.members) track(member.typeName);
        track(classInfo.parentName);
        for (const auto& modelName : cls->models)  track(modelName);
        for (const auto& ifaceName : cls->interfaces) track(ifaceName);
        for (const auto& method : cls->methods) {
            for (const auto& arg : method->args) track(arg.second);
            track(method->returnType);
            // Record the mangled method name so dfsCollect can recurse into body types
            classInfo.methodNames.push_back(ctx.qualify(method->name));
        }

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

        // Compute packed_size / padded_size for hot-reload calloc padding
        {
            int packed = (int)ctx.module.getDataLayout().getTypeAllocSize(structType);
            classInfo.packed_size = packed;
            int padded = 1;
            while (padded < packed) padded <<= 1;
            classInfo.padded_size = padded;
        }

        ctx.classes[modelName] = classInfo;
        LOG_DEBUG("AST Registered Model: " + modelName);
    } else if (auto enm = dynamic_cast<EnumDecl*>(node)) {
        std::string enumName = ctx.qualify(enm->name);
        std::map<std::string, int> enumValues;
        for (const auto& val : enm->values) {
            enumValues[val.first] = val.second;
        }
        ctx.enums[enumName] = enumValues;
        ctx.enumSources[enumName] = YAML::Dump(enm->sourceNode);
        LOG_DEBUG("AST Registered Enum: " + enumName);
    } else if (auto iface = dynamic_cast<InterfaceDecl*>(node)) {
        std::string ifaceName = ctx.qualify(iface->name);
        ctx.interfaceSources[ifaceName] = YAML::Dump(iface->sourceNode);
        LOG_DEBUG("AST Registered Interface: " + ifaceName);
    }
}
void Compiler::visitTopLevel(Node* node) {
    if (auto func = dynamic_cast<FunctionDecl*>(node)) {
        // Prevent duplicate IR generation if function was already compiled.
        // BUT: if a newer version is declared (higher `version:` value), update
        // sourceYaml + version in the symbol table so sys.bundle picks up the
        // new YAML for hot-reload bundles at runtime.
        std::string mangledName = func->name;
        if (ctx.functions.find(mangledName) != ctx.functions.end()) {
            auto& fi = ctx.functions[mangledName];
            if (func->version > fi.version) {
                fi.version    = func->version;
                fi.sourceYaml = YAML::Dump(func->sourceNode);
                // Propagate to unqualified name too (may be same key)
                if (ctx.functions.count(func->name))
                    ctx.functions[func->name] = fi;
                LOG_INFO("visitTopLevel: updated sourceYaml for " + mangledName +
                         " to v" + std::to_string(func->version));
            }
            return;
        }
        visitFunction(func);
    } else if (auto stmt = dynamic_cast<Stmt*>(node)) {
        visitStatement(stmt);
    } else if (auto cls = dynamic_cast<ClassDecl*>(node)) {
        // If this is a higher-version re-definition, update sourceYaml + version (not compiled_version)
        std::string qualName = ctx.qualify(cls->name);
        if (ctx.classes.count(qualName) && cls->version > ctx.classes[qualName].version) {
            ctx.classes[qualName].version    = cls->version;
            ctx.classes[qualName].sourceYaml = YAML::Dump(cls->sourceNode);
            if (ctx.classes.count(cls->name)) {
                ctx.classes[cls->name].version    = cls->version;
                ctx.classes[cls->name].sourceYaml = YAML::Dump(cls->sourceNode);
            }
            LOG_INFO("visitTopLevel: updated sourceYaml for " + cls->name + " to v"
                     + std::to_string(cls->version));
        }
        for (const auto& method : cls->methods) {
            std::string oldName = method->name;
            method->name = cls->name + "_" + method->name;
            auto it = method->args.insert(method->args.begin(), {"this", "ref<" + cls->name + ">"});
            
            std::string oldClassName = ctx.currentClassName;
            ctx.currentClassName = cls->name;
            
            visitFunction(method.get());
            
            ctx.currentClassName = oldClassName;
            
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
    funcInfo.version = func->version;
    funcInfo.compiled_version = func->version;  // locked at compile time
    // Assign fptable slot — preserve any pre-existing index (e.g. from an alias entry)
    if (ctx.functions.count(mangledName) && ctx.functions[mangledName].fptable_index >= 0)
        funcInfo.fptable_index = ctx.functions[mangledName].fptable_index;
    else
        funcInfo.fptable_index = ctx.fptable_next_index++;
    funcInfo.sourceYaml = YAML::Dump(func->sourceNode);
    // Track user-defined arg + return types for sys.bundle (recurses into list<T>/map<K,V>)
    auto trackType = [&](const std::string& typeName) {
        std::set<std::string> found;
        ctx.collectUserTypes(typeName, found);
        funcInfo.referencedTypes.insert(found.begin(), found.end());
    };
    for (const auto& arg : func->args) trackType(arg.second);
    trackType(func->returnType);
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

    // Compile Body (track referenced user types for sys.bundle)
    ctx.bundleTrackingTarget = mangledName;
    if (func->body) {
        visitBlockStmt(func->body.get());
    }
    ctx.bundleTrackingTarget.clear();

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
} // namespace AST
} // namespace AIML
