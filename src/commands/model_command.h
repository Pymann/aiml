#pragma once
#include "command.h"
#include "../core/context.h"
#include "../core/expression.h"

class ModelCommand : public AIMLCommand {
public:
    void compile(CompilerContext& ctx, const YAML::Node& node) override {
        // Syntax:
        // - model:
        //     name: Point
        //     fields:
        //       - [x, i32]
        //       - [y, i32]

        auto modelNode = node["model"];
        
        if (!modelNode["name"].IsScalar()) {
            LOG_ERROR("Model definition missing 'name'");
            return;
        }
        std::string modelName = modelNode["name"].as<std::string>();
        LOG_INFO("Defining Model: " + modelName);

        if (ctx.classes.count(modelName)) {
            LOG_ERROR("Type '" + modelName + "' already defined.");
            return;
        }

        CompilerContext::ClassInfo classInfo;
        classInfo.name = modelName;
        classInfo.isModel = true;

        if (modelNode["version"].IsScalar()) {
             classInfo.version = modelNode["version"].as<uint64_t>();
        }

        // Parse Fields
        std::vector<llvm::Type*> fieldTypes;
        if (modelNode["fields"].IsSequence()) {
            int index = 0;
            for (const auto& fieldNode : modelNode["fields"]) {
                if (fieldNode.IsSequence() && fieldNode.size() >= 2) {
                    std::string fieldName = fieldNode[0].as<std::string>();
                    std::string typeName = fieldNode[1].as<std::string>();
                    
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
                    member.access = AccessLevel::Public; // Models are data transfer objects, usually public
                    
                    // Default Value
                    if (fieldNode.size() >= 3) {
                        member.initValue = fieldNode[2];
                    }
                    
                    classInfo.members[fieldName] = member;
                }
            }
        }

        // Create LLVM Struct
        llvm::StructType* structType = llvm::StructType::create(ctx.context, modelName);
        structType->setBody(fieldTypes);
        classInfo.type = structType;
        
        ctx.classes[modelName] = classInfo;
    }
};
