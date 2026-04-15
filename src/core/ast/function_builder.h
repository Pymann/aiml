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

class FunctionBuilder : public StateHandler {
    std::unique_ptr<FunctionDecl> func;
    int capturedLine = -1;
    std::string currentKey;
    bool expectingKey = false;
    bool expectingValue = false;
    int mapDepth = 0;  // tracks nested map depth; 1 = outer function map
public:
    FunctionBuilder(Builder* b) : StateHandler(b), capturedLine(b->currentLine) {
        func = std::make_unique<FunctionDecl>();
    }
    bool OnMapStart() override;
    bool OnMapEnd() override;
    bool OnScalar(const std::string& value, const std::string& tag) override;
    bool OnSequenceStart() override;
    void receiveChildNode(std::unique_ptr<Node> node) override;
    std::unique_ptr<Node> finish() override;
};

} // namespace AST
} // namespace AIML
