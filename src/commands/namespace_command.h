#pragma once
#include "../core/context.h"
#include "command.h"
#include <iostream>
#include <stdexcept>

class NamespaceCommand : public AIMLCommand {
public:
    void compile(CompilerContext& ctx, const YAML::Node& node) override {
        // Syntax: - namespace: my.lib
        if (node["namespace"]) {
            std::string ns = node["namespace"].as<std::string>();
            
            // Validation: strict ban on "sys*" namespaces
            if (ns.rfind("sys", 0) == 0) {
                 LOG_ERROR("InternalSysNamespaceOverload: Namespace '" + ns + "' is reserved.");
                 throw std::runtime_error("InternalSysNamespaceOverload"); 
            }

            ctx.currentNamespace = ns;
            LOG_DEBUG("Set namespace to: " + ns);
        }
    }
};
