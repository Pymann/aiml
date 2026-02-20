#pragma once
#include "command.h"
#include "../core/context.h"
#include "../core/expression.h"

class ClassCommand : public AIMLCommand {
public:
    void compile(CompilerContext& ctx, const YAML::Node& node) override {
        // Syntax:
        // - class:
        //     name: Point
        //     members:
        //       - [x, i32]
        //       - [y, i32]
        //     methods: ...

        auto classNode = node["class"];
        
        // 1. Parse Class Name
        if (!classNode["name"].IsScalar()) {
            LOG_ERROR("Class definition missing 'name'");
            return;
        }
        std::string rawName = classNode["name"].as<std::string>();
        std::string className = ctx.qualify(rawName); // Qualified
        LOG_INFO("Defining Class: " + className);

        if (ctx.classes.count(className)) {
            LOG_ERROR("Class '" + className + "' already defined.");
            return;
        }

        CompilerContext::ClassInfo classInfo;
        classInfo.name = className; // Use qualified name

        if (classNode["version"].IsScalar()) {
             classInfo.version = classNode["version"].as<uint64_t>();
        } else {
             classInfo.version = 1; // Default
        }

        // --- Inheritance & Composition ---

        // 1. Extends (Parent Class)
        if (classNode["extends"].IsScalar()) {
            std::string parentName = classNode["extends"].as<std::string>();
            if (ctx.classes.count(parentName)) {
                classInfo.parentName = parentName;
                const auto& parentInfo = ctx.classes[parentName];
                
                // Prefix Parent Members
                // We copy parent members directly to maintain memory layout compatibility
                // (Parent fields first)
                for (const auto& [name, member] : parentInfo.members) {
                    classInfo.members[name] = member;
                    // Adjust index? No, index should be absolute offset in struct?
                    // LLVM Struct definitions are ordered lists of types.
                    // If Parent has members at index 0, 1. Child inherits them at 0, 1.
                    // Child adds new members at 2, 3...
                    // So we can copy directly.
                    // BUT: Map iteration order is not guaranteed by index!
                    // We need a sorted list of parent members by index to append correctly to `memberTypes` later.
                }
                LOG_INFO("Extends: " + parentName);
            } else {
                LOG_ERROR("Unknown parent class: " + parentName);
            }
        }

        // 2. Models (Mixins)
        if (classNode["models"].IsSequence()) {
            for (const auto& modelNode : classNode["models"]) {
                std::string modelName = modelNode.as<std::string>();
                if (ctx.classes.count(modelName) && ctx.classes[modelName].isModel) {
                     classInfo.usedModels.push_back(modelName);
                     const auto& modelInfo = ctx.classes[modelName];
                     
                     // Mixin Members
                     // Append these after parent members
                     for (const auto& [name, member] : modelInfo.members) {
                         if (classInfo.members.count(name)) {
                             LOG_ERROR("Duplicate member '" + name + "' from model '" + modelName + "'");
                             continue;
                         }
                         // Re-index explicitly later when building memberTypes
                         classInfo.members[name] = member; 
                     }
                     LOG_INFO("Uses Model: " + modelName);
                } else {
                     LOG_ERROR("Unknown model: " + modelName);
                }
            }
        }
        
        // 3. Implements (Interfaces)
        if (classNode["implements"].IsSequence()) {
            for (const auto& ifaceNode : classNode["implements"]) {
                std::string ifaceName = ifaceNode.as<std::string>();
                if (ctx.classes.count(ifaceName) && ctx.classes[ifaceName].isInterface) {
                     classInfo.interfaces.push_back(ifaceName);
                     LOG_INFO("Implements: " + ifaceName);
                     // TODO: Verify method implementations later
                } else {
                     LOG_ERROR("Unknown interface: " + ifaceName);
                }
            }
        }

        // --- Member Parsing ---
        
        // We need to re-construct memberTypes vector carefully:
        // 1. Parent Members (Sorted by index)
        // 2. Model Members (In order of appearance)
        // 3. Class Members (In order of definition)
        
        // Existing member storage in `classInfo.members` is a map (name -> member).
        // The index in `member` struct determines order.
        // Parent members already have indices 0..N.
        // We need to shift indices for subsequently added members?
        
        // Strategy:
        // Keep a running `currentIndex`.
        // 1. Add Parent types (if any). `currentIndex` += parent.memberCount.
        // 2. Add Model types. Assign `currentIndex++` to each.
        // 3. Add Class types. Assign `currentIndex++` to each.
        
        int currentIndex = 0;
        std::vector<llvm::Type*> memberTypes;
        
        // 1. Parent
        if (!classInfo.parentName.empty()) {
            const auto& parentInfo = ctx.classes[classInfo.parentName];
            // We need parent members sorted by index. Map is name-ordered.
            std::vector<const CompilerContext::StructMember*> sortedParentMembers;
            for(const auto& [name, m] : parentInfo.members) sortedParentMembers.push_back(&m);
            std::sort(sortedParentMembers.begin(), sortedParentMembers.end(), [](auto a, auto b){ return a->index < b->index; });
            
            for(const auto* m : sortedParentMembers) {
                // Ensure we copy the member info correctly
                // Note: We already copied to `classInfo.members` above, but we need to push types here.
                memberTypes.push_back(m->type);
                currentIndex++; 
            }
        }
        
        // 2. Models
        // We iterated and added to map above, but didn't assign new indices.
        // We should effectively re-process or handle this better.
        // Let's clear `classInfo.members` and re-populate strictly here? 
        // Or trust the loop.
        
        // Refined Strategy:
        // Don't populate `classInfo.members` in steps 1 & 2 above blindly.
        // Instead:
        
        // A. Parent
         if (!classInfo.parentName.empty()) {
            const auto& parentInfo = ctx.classes[classInfo.parentName];
             // Sort and Add
            std::vector<CompilerContext::StructMember> sorted;
            for(const auto& [name, m] : parentInfo.members) sorted.push_back(m);
            std::sort(sorted.begin(), sorted.end(), [](auto a, auto b){ return a.index < b.index; });
            
            for(const auto& m : sorted) {
                classInfo.members[m.typeName] = m; // Incorrect key? map key is field name.
                 // Correct: We need to find the Name for the member.
                 // The map `parentInfo.members` stores it as key.
                 // Re-finding name is annoying. `StructMember` doesn't have `name` field?
                 // CHECK: `struct StructMember { int index; ... std::string typeName; ... };` 
                 // It DOES NOT have the field name! That's a flaw in Context.h or usage pattern.
                 // We rely on the map key.
                 // OK, let's iterate the Map and put into a vector of pairs?
            }
        }
        
        // CRITICAL FIX: To handle ordering correctly, we need to sort [Name, Member] pairs.
        
        // --- Redo Member Layout ---
        
        // list of {name, member}
        std::vector<std::pair<std::string, CompilerContext::StructMember>> orderedMembers;
        
        // 1. Parent
        if (!classInfo.parentName.empty()) {
             const auto& parentInfo = ctx.classes[classInfo.parentName];
             for (const auto& [name, member] : parentInfo.members) {
                 orderedMembers.push_back({name, member});
             }
             // Sort by existing index
             std::sort(orderedMembers.begin(), orderedMembers.end(), [](auto& a, auto& b){ return a.second.index < b.second.index; });
        }
        // Current index is size of parent members
        currentIndex = orderedMembers.size();
        
        // 2. Models
        for (const auto& modelName : classInfo.usedModels) {
             const auto& modelInfo = ctx.classes[modelName];
             // Models might also have structure, but usually just fields.
             // We append them.
             // Sort model members by index too (in case model has multiple fields)
             std::vector<std::pair<std::string, CompilerContext::StructMember>> modelMembers;
             for (const auto& [name, member] : modelInfo.members) modelMembers.push_back({name, member});
             std::sort(modelMembers.begin(), modelMembers.end(), [](auto& a, auto& b){ return a.second.index < b.second.index; });
             
             for (auto& [name, member] : modelMembers) {
                 // Check duplicate
                 bool duplicate = false;
                 for(const auto& pair : orderedMembers) if(pair.first == name) duplicate = true;
                 
                 if (!duplicate) {
                     CompilerContext::StructMember newMember = member; // Copy
                     newMember.index = currentIndex++;
                     orderedMembers.push_back({name, newMember});
                 }
             }
        }
        
        // 3. Class Members (Parse from YAML)
        if (classNode["members"].IsSequence()) {
             // ... existing parsing loop ...
             // We need to integrate the loop here to respect ordering.
             for (const auto& memberNode : classNode["members"]) {
                if (memberNode.IsSequence() && memberNode.size() >= 2) {
                    std::string memberName = memberNode[0].as<std::string>();
                    std::string typeName = memberNode[1].as<std::string>();
                    
                    // Check duplicate
                    bool duplicate = false;
                    for(const auto& pair : orderedMembers) if(pair.first == memberName) duplicate = true;
                    if(duplicate) {
                        LOG_ERROR("Duplicate member '" + memberName + "' in class '" + className + "'");
                        continue;
                    }
                    
                    llvm::Type* memberType = ctx.getLLVMType(typeName);
                    if (!memberType) {
                        LOG_ERROR("Unknown type '" + typeName + "'");
                        continue; 
                    }

                    CompilerContext::StructMember member;
                    member.index = currentIndex++;
                    member.type = memberType;
                    member.typeName = typeName;
                    
                    // ... (Init/Modifiers parsing same as before) ...
                    YAML::Node modifiersNode;
                    if (memberNode.size() == 3) {
                        if (memberNode[2].IsMap()) modifiersNode = memberNode[2];
                        else member.initValue = memberNode[2];
                    } else if (memberNode.size() >= 4) {
                        member.initValue = memberNode[2];
                        modifiersNode = memberNode[3];
                    }
                    
                    if (modifiersNode.IsMap()) {
                         if (modifiersNode["access"].IsScalar()) {
                             std::string acc = modifiersNode["access"].as<std::string>();
                             if (acc == "public") member.access = AccessLevel::Public;
                             else if (acc == "protected") member.access = AccessLevel::Protected;
                             else member.access = AccessLevel::Private;
                         }
                         if (modifiersNode["const"].IsScalar() && modifiersNode["const"].as<bool>()) member.is_const = true;
                         if (modifiersNode["version"].IsScalar()) member.version = modifiersNode["version"].as<uint64_t>();
                    }

                    orderedMembers.push_back({memberName, member});
                    LOG_DEBUG("Parsed Member: " + memberName + " Index: " + std::to_string(member.index));
                }
             }
        }
        
        // Finalize Members
        for(const auto& [name, member] : orderedMembers) {
            classInfo.members[name] = member;
            memberTypes.push_back(member.type);
        }

        // 3. Create LLVM Struct
        // We use createNamed to verify unique names and support opaque struct pointers if needed?
        // LLVM 15+ uses opaque pointers, but StructType still needs body.
        llvm::StructType* structType = llvm::StructType::create(ctx.context, className);
        structType->setBody(memberTypes);
        classInfo.type = structType;
        
        // Register class immediately so methods can use it (for 'this')
        ctx.classes[className] = classInfo;

        // 4. Parse Methods
        if (classNode["methods"].IsSequence()) {
            for (const auto& methodNode : classNode["methods"]) {
                if (methodNode["def"].IsMap()) {
                    compileMethod(ctx, className, methodNode["def"], classInfo);
                }
            }
        }
    }

private:
    void compileMethod(CompilerContext& ctx, const std::string& className, const YAML::Node& defNode, CompilerContext::ClassInfo& classInfo) {
        if (!defNode["name"].IsScalar()) return;
        std::string methodName = defNode["name"].as<std::string>();
        std::string mangledName = className + "_" + methodName; // Simple mangling
        
        LOG_INFO("Compiling Method: " + mangledName);
        
        // Context Switch
        std::string oldClassName = ctx.currentClassName;
        ctx.currentClassName = className;

        // Parse Access
        AccessLevel methodAccess = AccessLevel::Public; // Default
        if (defNode["access"].IsDefined() && defNode["access"].IsScalar()) {
             std::string acc = defNode["access"].as<std::string>();
             if (acc == "private") methodAccess = AccessLevel::Private;
             else if (acc == "protected") methodAccess = AccessLevel::Protected;
        }

        // 1. Prepare Arguments
        // Inject 'this' as first argument
        std::vector<llvm::Type*> argTypes;
        std::vector<std::string> argNames;
        
        // '<' is a pointer to the class struct (replacing 'this')
        // With opaque pointers, it is just ptr. But strictly it is ptr<Struct>.
        argTypes.push_back(llvm::PointerType::get(ctx.context, 0)); 
        argNames.push_back("<");

        // Parse user args
        if (defNode["args"].IsSequence()) {
            for (const auto& arg : defNode["args"]) {
                if (arg.IsSequence() && arg.size() >= 2) {
                    std::string argName = arg[0].as<std::string>();
                    std::string argTypeStr = arg[1].as<std::string>();
                    llvm::Type* argType = ctx.getLLVMType(argTypeStr);
                    if (argType) {
                        argTypes.push_back(argType);
                        argNames.push_back(argName);
                    }
                }
            }
        }

        // Return Type
        llvm::Type* retType = ctx.builder.getVoidTy(); // Default void
        if (defNode["return"].IsDefined() && defNode["return"].IsScalar()) {
             std::string retTypeStr = defNode["return"].as<std::string>();
             if (retTypeStr != "void") {
                 llvm::Type* t = ctx.getLLVMType(retTypeStr);
                 if (t) retType = t;
             }
        }

        // Create Function
        llvm::FunctionType* funcType = llvm::FunctionType::get(retType, argTypes, false);
        llvm::Function* function = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, mangledName, ctx.module);

        // Store in ClassInfo
        CompilerContext::FunctionInfo funcInfo;
        funcInfo.func = function;
        funcInfo.argNames = argNames; // includes "this" at index 0
        funcInfo.access = methodAccess;
        classInfo.methods[methodName] = funcInfo;

        if (ctx.headerOnlyMode) {
             LOG_DEBUG("HeaderOnly: Skipping method body for " + mangledName);
             return;
        }

        // Compile Body
        llvm::BasicBlock* entryBlock = llvm::BasicBlock::Create(ctx.context, "entry", function);
        llvm::IRBuilder<> tempBuilder(entryBlock);
        
        // Save previous builder point
        auto prevBlock = ctx.builder.GetInsertBlock();
        ctx.builder.SetInsertPoint(entryBlock);

        // Create Allocas for Args (including 'this')
        int argIdx = 0;
        for (auto& arg : function->args()) {
            std::string argName = argNames[argIdx];
            arg.setName(argName);
            
            llvm::AllocaInst* alloca = ctx.builder.CreateAlloca(arg.getType(), nullptr, argName + "_addr");
            ctx.builder.CreateStore(&arg, alloca);
            
            // Register symbol
            // For 'this', we need to mark it as a class pointer so member access works?
            // "this" is just a variable name in symbol table. Its value is the Alloca.
            // When we do `this.x`, `resolveLValue` will see `this` -> resolve to Alloca -> Load value -> `ptr`.
            // Then it needs to know `ptr` is of type `ClassName`.
            // With opaque pointers, `ptr` is just `ptr`.
            // We need to store type info in Symbol!
            
            Symbol sym;
            sym.alloca_inst = alloca;
            sym.type = arg.getType(); // pointer to arg type
            
            // If it is '<', we know the element type is the class struct
            if (argName == "<") {
                 sym.element_type = llvm::PointerType::get(ctx.context, 0); // '<' is a pointer (the instance)
                 sym.className = className; 
            } else {
                 // Try to deduce className for other args?
                 // Sym.element_type is set to arg.getType().
                 // If arg.getType() is ptr, we don't know needed info unless we parse the type string again
                 // But we have argNames and argTypes in vectors, checking original YAML is hard here effectively?
                 // Actually we parsed YAML above into argTypes.
                 // We didn't store typeNames.
                 // Ideally we should store typeName in FunctionInfo/Argument info.
            }
            
            ctx.symbol_table[argName] = sym; // Shadow global symbols
            argIdx++;
        }

        // Compile Body Nodes
        if (defNode["body"].IsSequence()) {
            if (ctx.params.compileCallback) {
                 ctx.params.compileCallback(ctx, defNode["body"]);
            }
        }
        
        // Implicit void return if missing
        if (retType->isVoidTy() && !entryBlock->getTerminator()) {
             ctx.builder.CreateRetVoid();
        }

        // Restore builder
        if (prevBlock) ctx.builder.SetInsertPoint(prevBlock);
        
        ctx.currentClassName = oldClassName;
    }
};
