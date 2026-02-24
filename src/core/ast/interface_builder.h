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

class InterfaceBuilder : public StateHandler {
    std::unique_ptr<InterfaceDecl> interfaceNode;
    std::string currentKey;
    bool expectingKey = false;
    bool insideMethods = false;
    
    bool insideMethodDef = false;
    MethodSignature currentMethod;
public:
    InterfaceBuilder(Builder* b) : StateHandler(b) { interfaceNode = std::make_unique<InterfaceDecl>(); }
    bool OnMapStart() override;
    bool OnMapEnd() override;
    bool OnScalar(const std::string& value, const std::string& tag) override;
    bool OnSequenceStart() override;
    bool OnSequenceEnd() override;
    std::unique_ptr<Node> finish() override;
};

} // namespace AST
} // namespace AIML
