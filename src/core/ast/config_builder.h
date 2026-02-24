#pragma once
#include <memory>
#include <string>
#include <vector>
#include <stack>
#include "../ast_nodes.h"

namespace AIML {
namespace AST {

class Builder;
class StateHandler;

class ConfigBuilder : public StateHandler {
    std::unique_ptr<ConfigDecl> configNode;
    std::string currentKey;
    bool expectingKey = false;
    bool insideConfig = false;
public:
    ConfigBuilder(Builder* b) : StateHandler(b) { configNode = std::make_unique<ConfigDecl>(); }
    bool OnMapStart() override;
    bool OnMapEnd() override;
    bool OnScalar(const std::string& value, const std::string& tag) override;
    std::unique_ptr<Node> finish() override;
};

} // namespace AST
} // namespace AIML
