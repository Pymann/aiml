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

class ImportBuilder : public StateHandler {
    std::unique_ptr<ImportDecl> importNode;
    std::string currentKey;
    bool expectingKey = false;
public:
    ImportBuilder(Builder* b) : StateHandler(b) { importNode = std::make_unique<ImportDecl>(); }
    bool OnMapStart() override { expectingKey = true; return true; }
    bool OnMapEnd() override { builder->popState(); return true; }
    bool OnScalar(const std::string& value, const std::string& tag) override;
    std::unique_ptr<Node> finish() override;
};

} // namespace AST
} // namespace AIML
