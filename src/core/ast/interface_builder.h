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
    int  methodDepth = 0;          // 1 = inside [name, ret, args]; 2 = inside the args sub-sequence; 3 = inside [name,type] arg pair
    MethodSignature currentMethod;
    std::pair<std::string,std::string> currentArg;  // accumulates [name, type] while methodDepth==3
    bool curArgGotName = false;
    // spec/61 §3.1: an arg pair may carry a 3rd modifier map { lifetime: consumed }.
    std::string curArgLifetime = "scope";
    bool inArgModMap = false;       // inside the { ... } modifier map of an arg pair
    bool argModExpectKey = true;
    std::string argModKey;
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
