#pragma once
#include "command.h"
#include "../core/context.h"
#include "../../logger.h"

class ConfigCommand : public AIMLCommand {
public:
    void compile(CompilerContext& ctx, const YAML::Node& node) override {
        // Syntax: 
        // - config:
        //     entrypoint: "app_start"
        //     stack_size: 1048576
        //     heap_size: 16777216
        //     target: "x86_64"
        //     version: "1.0"
        
        auto cfgNode = node["config"];
        if (!cfgNode.IsDefined() || !cfgNode.IsMap()) {
            LOG_ERROR("Invalid config definition: Expected map.");
            return;
        }

        if (cfgNode["entrypoint"].IsDefined()) {
            ctx.config.entryPointName = cfgNode["entrypoint"].as<std::string>();
            LOG_DEBUG("Configured entry point: " + ctx.config.entryPointName);
        }

        if (cfgNode["stack_size"].IsDefined()) {
            ctx.config.stackSize = cfgNode["stack_size"].as<size_t>();
            LOG_DEBUG("Configured stack size: " + std::to_string(ctx.config.stackSize));
        }

        if (cfgNode["heap_size"].IsDefined()) {
            ctx.config.heapSize = cfgNode["heap_size"].as<size_t>();
            LOG_DEBUG("Configured heap size: " + std::to_string(ctx.config.heapSize));
        }

        if (cfgNode["target"].IsDefined()) {
            ctx.config.target = cfgNode["target"].as<std::string>();
            LOG_DEBUG("Configured target: " + ctx.config.target);
        }

        if (cfgNode["version"].IsDefined()) {
            ctx.config.version = cfgNode["version"].as<std::string>();
            LOG_DEBUG("Configured version: " + ctx.config.version);
        }
    }
};
