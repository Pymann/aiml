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

class StatementBuilder : public StateHandler {
    std::string type;
    std::vector<std::pair<std::string, std::string>> args;
    std::unique_ptr<Expr> exprArg;
    bool expectingValue = false;
    bool is_const = false;
    bool is_volatile = false;
public:
    StatementBuilder(Builder* b, std::string t) : StateHandler(b), type(t) {}
    bool OnSequenceStart() override;
    bool OnSequenceEnd() override;
    bool OnScalar(const std::string& value, const std::string& tag) override;
    bool OnMapStart() override;
    bool OnMapEnd() override;
    void receiveChildNode(std::unique_ptr<Node> node) override;
    std::unique_ptr<Node> finish() override;
};

} // namespace AST
} // namespace AIML
