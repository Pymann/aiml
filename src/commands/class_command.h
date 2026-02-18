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
        std::string className = classNode["name"].as<std::string>();
        LOG_INFO("Defining Class: " + className);

        if (ctx.classes.count(className)) {
            LOG_ERROR("Class '" + className + "' already defined.");
            return;
        }

        CompilerContext::ClassInfo classInfo;
        classInfo.name = className;

        if (classNode["version"].IsScalar()) {
             classInfo.version = classNode["version"].as<uint64_t>();
        } else {
             classInfo.version = 1; // Default
        }

        // 2. Parse Members
        std::vector<llvm::Type*> memberTypes;
        if (classNode["members"].IsSequence()) {
            int index = 0;
            for (const auto& memberNode : classNode["members"]) {
                if (memberNode.IsSequence() && memberNode.size() >= 2) {
                    std::string memberName = memberNode[0].as<std::string>();
                    std::string typeName = memberNode[1].as<std::string>();
                    
                    llvm::Type* memberType = ctx.getLLVMType(typeName);
                    if (!memberType) {
                        LOG_ERROR("Unknown type '" + typeName + "' for member '" + memberName + "' in class '" + className + "'");
                        continue; 
                    }

                    memberTypes.push_back(memberType);
                    
                    CompilerContext::StructMember member;
                    member.index = index++;
                    member.type = memberType;
                    member.typeName = typeName;
                    
                    // Parse Init Value and Modifiers
                    YAML::Node modifiersNode;
                    
                    if (memberNode.size() == 3) {
                        // [name, type, init] OR [name, type, modifiers]
                        if (memberNode[2].IsMap()) {
                            modifiersNode = memberNode[2];
                        } else {
                            member.initValue = memberNode[2];
                        }
                    } else if (memberNode.size() >= 4) {
                        // [name, type, init, modifiers]
                        member.initValue = memberNode[2];
                        modifiersNode = memberNode[3];
                    }
                    
                    // Process Modifiers
                    if (modifiersNode.IsMap()) {
                        if (modifiersNode["access"].IsScalar()) {
                            std::string acc = modifiersNode["access"].as<std::string>();
                            if (acc == "public") member.access = AccessLevel::Public;
                            else if (acc == "protected") member.access = AccessLevel::Protected;
                            else member.access = AccessLevel::Private;
                        }
                        
                        if (modifiersNode["const"].IsScalar() && modifiersNode["const"].as<bool>()) {
                            member.is_const = true;
                            LOG_DEBUG("Member '" + memberName + "' marked as CONST");
                        }
                        
                        if (modifiersNode["version"].IsScalar()) {
                            member.version = modifiersNode["version"].as<uint64_t>();
                        }
                    }
                    
                    classInfo.members[memberName] = member;
                    LOG_DEBUG("Parsed Member: " + memberName + " Index: " + std::to_string(index-1) + " Init: " + (member.initValue.IsDefined() ? "YES" : "NO"));
                }
            }
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
        if (defNode["access"].IsScalar()) {
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
        if (defNode["return"].IsScalar()) {
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
