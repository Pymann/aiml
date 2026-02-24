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

class RootState : public StateHandler {
    bool expectingKey = false;
    std::string currentKey = "";
public:
    RootState(Builder* b) : StateHandler(b) {}
    bool OnMapStart() override;
    bool OnMapEnd() override;
    bool OnScalar(const std::string& value, const std::string& tag) override;
    void receiveChildNode(std::unique_ptr<Node> node) override;
};

} // namespace AST
} // namespace AIML
