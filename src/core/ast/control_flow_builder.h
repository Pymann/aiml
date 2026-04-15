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

class ControlFlowBuilder : public StateHandler {
    std::string type;
    int capturedLine = -1;
    std::unique_ptr<Expr> condition;
    std::unique_ptr<BlockStmt> thenBranch;
    std::unique_ptr<BlockStmt> elseBranch; 
    std::unique_ptr<BlockStmt> body;
    
    std::string currentKey;
    bool expectingKey = false;
public:
    ControlFlowBuilder(Builder* b, std::string t) : StateHandler(b), type(t), capturedLine(b->currentLine) {}
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
