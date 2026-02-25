#define DEBUG_MODE
#include "ast_builder.h"
#include "ast/root_state.h"
#include "ast/statement_builder.h"
#include "ast/block_builder.h"
#include "ast/class_builder.h"
#include "ast/model_builder.h"
#include "ast/interface_builder.h"
#include "ast/enum_builder.h"
#include "ast/namespace_builder.h"
#include "ast/import_builder.h"
#include "ast/config_builder.h"
#include "ast/function_builder.h"
#include "ast/control_flow_builder.h"
#include "ast/expression_builder.h"
#include "ast/try_builder.h"
#include "../../logger.h"
#include <iostream>
#include <yaml-cpp/yaml.h>

namespace AIML {
namespace AST {

std::unique_ptr<Expr> parseInterpolatedString(const std::string& input);

std::unique_ptr<Expr> nodeToExpr(const YAML::Node& node) {
    if (node.IsScalar()) {
        std::string val = node.as<std::string>();
        if (node.Tag() == "!") {
            if (val.find("${") != std::string::npos) return parseInterpolatedString(val);
            return std::make_unique<LiteralExpr>(val, "string");
        }
        if (val == "true" || val == "false" || (val.length() > 0 && isdigit(val[0])) || (val.length() > 0 && val[0] == '-')) {
            return std::make_unique<LiteralExpr>(val, "unknown");
        }
        return std::make_unique<VariableExpr>(val);
    }
    if (node.IsMap()) {
        if (node["new"]) {
            std::string className;
            if (node["new"].IsScalar()) className = node["new"].as<std::string>();
            else if (node["new"].IsMap() && node["new"].begin()->first.as<std::string>() == "new") {} // Ignore
            
            auto newExpr = std::make_unique<NewExpr>(className);
            for (auto it = node.begin(); it != node.end(); ++it) {
                std::string key = it->first.as<std::string>();
                if (key != "new") {
                    newExpr->initValues[key] = nodeToExpr(it->second);
                }
            }
            return newExpr;
        }

        std::string op = node.begin()->first.as<std::string>();
        auto argsNode = node.begin()->second;
        LOG_DEBUG("nodeToExpr: Map op=" + op + " isSeq=" + std::to_string(argsNode.IsSequence()) + " size=" + std::to_string(argsNode.size()));
        
        if (op == "call" && argsNode.IsSequence() && argsNode.size() > 0) {
            std::string funcName;
            if (argsNode[0].IsScalar()) funcName = argsNode[0].as<std::string>();
            else if (argsNode[0].IsMap() && argsNode[0].begin()->first.as<std::string>() == "call") {
                // Should not happen as function name
            }
            auto callExpr = std::make_unique<CallExpr>(funcName);
            for(size_t i=1; i<argsNode.size(); ++i) {
                callExpr->args.push_back(nodeToExpr(argsNode[i]));
            }
            return callExpr;
        }

        if (op == "spawn" && argsNode.IsSequence() && argsNode.size() >= 1) {
            std::string funcName = argsNode[0].IsScalar() ? argsNode[0].as<std::string>() : "unknown";
            auto spawnExpr = std::make_unique<SpawnExpr>(funcName);
            for (size_t i = 1; i < argsNode.size(); ++i)
                spawnExpr->args.push_back(nodeToExpr(argsNode[i]));
            return spawnExpr;
        }
        
        // Unary operators: single-argument maps like { '?': [p] }, { '&': [x] }
        if (argsNode.IsSequence() && argsNode.size() == 1) {
            return std::make_unique<UnaryExpr>(op, nodeToExpr(argsNode[0]));
        }

        // Binary operators
        if (argsNode.IsSequence() && argsNode.size() == 2) {
             return std::make_unique<BinaryExpr>(op, nodeToExpr(argsNode[0]), nodeToExpr(argsNode[1]));
        }
    }
    return nullptr;
}

std::unique_ptr<Expr> parseScalarExpr(const std::string& val, const std::string& tag) {
    if (tag == "!") {
        if (val.find("${") != std::string::npos) return parseInterpolatedString(val);
        else return std::make_unique<LiteralExpr>(val, "string");
    } else if (val == "true" || val == "false" || isdigit(val[0]) || (val.length() > 0 && val[0] == '-')) {
        return std::make_unique<LiteralExpr>(val, "unknown");
    } else if (val.find("${") != std::string::npos) {
        return parseInterpolatedString(val);
    } else if (val.length() > 0 && (val[0] == '"' || val[0] == '\'')) {
        return std::make_unique<LiteralExpr>(val, "string");
    } else {
        return std::make_unique<VariableExpr>(val);
    }
}

std::unique_ptr<Expr> parseInterpolatedString(const std::string& input) {
    if (input.find("${") == std::string::npos) {
        return std::make_unique<LiteralExpr>(input, "string");
    }
    
    auto interp = std::make_unique<StringInterpolationExpr>();
    size_t pos = 0;
    while (pos < input.length()) {
        size_t start = input.find("${", pos);
        if (start == std::string::npos) {
            std::string suffix = input.substr(pos);
            if (!suffix.empty()) interp->parts.push_back(std::make_unique<LiteralExpr>(suffix, "string"));
            break;
        }
        
        if (start > pos) {
            interp->parts.push_back(std::make_unique<LiteralExpr>(input.substr(pos, start - pos), "string"));
        }
        
        size_t end = std::string::npos;
        int braceDepth = 0;
        for (size_t i = start + 2; i < input.length(); ++i) {
             if (input[i] == '{') braceDepth++;
             else if (input[i] == '}') {
                 if (braceDepth > 0) braceDepth--;
                 else {
                      end = i;
                      break;
                 }
             }
        }
        if (end == std::string::npos) break; // Malformed
        
        std::string exprStr = input.substr(start + 2, end - start - 2);
        try {
            YAML::Node node = YAML::Load(exprStr);
            LOG_DEBUG("parseInterpolatedString: Loaded YAML node for: " + exprStr);
            auto astExpr = nodeToExpr(node);
            if (astExpr) {
                LOG_DEBUG("parseInterpolatedString: Successfully created AST expr for: " + exprStr);
                interp->parts.push_back(std::move(astExpr));
            } else {
                interp->parts.push_back(std::make_unique<LiteralExpr>("${" + exprStr + "}", "string"));
            }
        } catch (...) {
            interp->parts.push_back(std::make_unique<LiteralExpr>("${" + exprStr + "}", "string"));
        }
        
        pos = end + 1;
    }
    
    return interp;
}

Builder::Builder() {
    program = std::make_unique<Program>();
    stateStack.push(std::make_unique<RootState>(this));
}

std::unique_ptr<Program> Builder::takeProgram() {
    return std::move(program);
}

void Builder::addDeclaration(std::unique_ptr<Node> node) {
    if (program && node) {
        program->declarations.push_back(std::move(node));
    }
}

void Builder::pushState(std::unique_ptr<StateHandler> state) {
    LOG_DEBUG("Pushing new state into Builder stack.");
    stateStack.push(std::move(state));
}

void Builder::popState() {
    if (stateStack.size() > 1) {
        auto finishedState = std::move(stateStack.top());
        stateStack.pop();
        
        LOG_DEBUG("Popping state in Builder");
        auto finishedNode = finishedState->finish();
        if (finishedNode) {
            LOG_DEBUG("Passing finishedNode to parent state.");
            stateStack.top()->receiveChildNode(std::move(finishedNode));
        } else {
            LOG_DEBUG("Finished state returned nullptr.");
        }
    }
}

// Event Router
void Builder::OnScalar(const YAML::Mark&, const std::string& tag, YAML::anchor_t, const std::string& value) {
    LOG_DEBUG("Router OnScalar: " + value);
    stateStack.top()->OnScalar(value, tag);
}

void Builder::OnSequenceStart(const YAML::Mark&, const std::string&, YAML::anchor_t, YAML::EmitterStyle::value) {
    LOG_DEBUG("Router OnSequenceStart");
    stateStack.top()->OnSequenceStart();
}

void Builder::OnSequenceEnd() {
    LOG_DEBUG("Router OnSequenceEnd");
    stateStack.top()->OnSequenceEnd();
}

void Builder::OnMapStart(const YAML::Mark&, const std::string&, YAML::anchor_t, YAML::EmitterStyle::value) {
    LOG_DEBUG("Router OnMapStart");
    stateStack.top()->OnMapStart();
}

void Builder::OnMapEnd() {
    LOG_DEBUG("Router OnMapEnd");
    stateStack.top()->OnMapEnd();
}


} // namespace AST
} // namespace AIML
