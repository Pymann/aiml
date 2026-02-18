#pragma once
#include "command.h"
#include "../core/context.h"

class InterfaceCommand : public AIMLCommand {
public:
    void compile(CompilerContext& ctx, const YAML::Node& node) override {
        // Syntax:
        // - interface:
        //     name: Printable
        //     methods:
        //       - [print, void, []] 

        auto ifaceNode = node["interface"];
        
        if (!ifaceNode["name"].IsScalar()) {
            LOG_ERROR("Interface definition missing 'name'");
            return;
        }
        std::string ifaceName = ifaceNode["name"].as<std::string>();
        LOG_INFO("Defining Interface: " + ifaceName);

        if (ctx.classes.count(ifaceName)) {
            LOG_ERROR("Type '" + ifaceName + "' already defined.");
            return;
        }

        CompilerContext::ClassInfo classInfo;
        classInfo.name = ifaceName;
        classInfo.isInterface = true;
        // Interface has no fields (normally), but we'll use ClassInfo to store method signatures.
        // LLVM type for interface? Usually opaque or vtable pointer. 
        // For now, let's make it an empty struct or just a phantom type for checking.
        // If we want to support interface pointers (polymorphism), we need a vtable layout.
        // For this iteration, we just track method signatures for validation in 'implements'.
        
        classInfo.type = llvm::StructType::create(ctx.context, ifaceName); // Empty struct

        // Parse Methods
        if (ifaceNode["methods"].IsSequence()) {
            for (const auto& methodNode : ifaceNode["methods"]) {
                // Format: [name, returnType, [args...]]
                // OR map format? Let's stick to simple list for interface brevity or re-use def-like structure?
                // aiml syntax usually: 
                // methods:
                //   - def: { name: print, args: [], return: void, body: [] } -> Interface has no body.
                // Simplified:
                // methods:
                //   - [print, void, [[arg1, type]]]
                
                if (methodNode.IsSequence() && methodNode.size() >= 2) {
                    std::string methodName = methodNode[0].as<std::string>();
                    std::string retTypeStr = methodNode[1].as<std::string>();
                    
                    CompilerContext::FunctionInfo funcInfo;
                    // We don't create an LLVM Function for interface methods (no impl).
                    // We just store signature.
                    
                    // We need to store args to validate implementation
                    // funcInfo.argNames ... 
                    // Wait, Context.h FunctionInfo expects llvm::Function*.
                    // We might need a "Signature" struct or just use dummy null func?
                    // Let's use metadata in ClassInfo.methods
                    
                    // For now, we mainly validate existence. Signature check is bonus.
                    classInfo.methods[methodName] = funcInfo; // Placeholder
                    LOG_DEBUG("Interface Method: " + methodName);
                }
            }
        }
        
        ctx.classes[ifaceName] = classInfo;
    }
};
