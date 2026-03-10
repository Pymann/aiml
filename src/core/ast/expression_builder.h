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

class ExpressionBuilder : public StateHandler {
    std::string op;
    bool expectingKey = false;
    bool expectingArgs = false;
    bool hadSequence = false;   // true if args came from a [...] sequence (operator), false for dict
    std::vector<std::unique_ptr<Expr>> args;
public:
    ExpressionBuilder(Builder* b) : StateHandler(b) {}
    bool OnMapStart() override;
    bool OnMapEnd() override;
    bool OnScalar(const std::string& value, const std::string& tag) override;
    bool OnSequenceStart() override;
    bool OnSequenceEnd() override;
    void receiveChildNode(std::unique_ptr<Node> node) override;
    std::unique_ptr<Node> finish() override;
};

} // namespace AST
} // namespace AIML
