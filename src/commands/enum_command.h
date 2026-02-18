#pragma once
#include "command.h"
#include "../core/context.h"
#include "../../logger.h"

class EnumCommand : public AIMLCommand {
public:
    void compile(CompilerContext& ctx, const YAML::Node& node) override {
        // Syntax:
        // - enum:
        //     name: Color
        //     values:
        //       - Red
        //       - Green: 1
        
        auto enumNode = node["enum"];
        if (!enumNode["name"].IsScalar()) {
            LOG_ERROR("Enum definition missing 'name'");
            return;
        }
        
        std::string enumName = enumNode["name"].as<std::string>();
        
        if (ctx.enums.count(enumName)) {
            LOG_ERROR("Enum '" + enumName + "' already defined.");
            return;
        }

        std::map<std::string, int> values;
        int nextValue = 0;
        
        if (enumNode["values"].IsSequence()) {
            for (const auto& valNode : enumNode["values"]) {
                if (valNode.IsScalar()) {
                    // - Red
                    std::string valName = valNode.as<std::string>();
                    values[valName] = nextValue++;
                    LOG_DEBUG("Enum " + enumName + "." + valName + " = " + std::to_string(nextValue-1));
                } else if (valNode.IsMap()) {
                    // - Green: 10
                    std::string valName = valNode.begin()->first.as<std::string>();
                    int val = valNode.begin()->second.as<int>();
                    values[valName] = val;
                    nextValue = val + 1;
                    LOG_DEBUG("Enum " + enumName + "." + valName + " = " + std::to_string(val));
                }
            }
        }
        
        ctx.enums[enumName] = values;
        LOG_INFO("Defined Enum: " + enumName);
    }
};
