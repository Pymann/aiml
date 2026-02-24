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

class NamespaceBuilder : public StateHandler {
    std::unique_ptr<NamespaceDecl> nsNode;
    std::string currentKey;
    bool expectingKey = false;
public:
    NamespaceBuilder(Builder* b) : StateHandler(b) { nsNode = std::make_unique<NamespaceDecl>(); }
    bool OnMapStart() override { expectingKey = true; return true; }
    bool OnMapEnd() override { builder->popState(); return true; }
    bool OnScalar(const std::string& value, const std::string& tag) override;
    std::unique_ptr<Node> finish() override;
};

} // namespace AST
} // namespace AIML
