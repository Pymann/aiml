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

class EnumBuilder : public StateHandler {
    std::unique_ptr<EnumDecl> enumNode;
    std::string currentKey;
    bool expectingKey = false;
    bool insideValues = false;
    int overrideValue = -1;
    std::string activeValueName;
public:
    EnumBuilder(Builder* b) : StateHandler(b) { enumNode = std::make_unique<EnumDecl>(); }
    bool OnMapStart() override;
    bool OnMapEnd() override;
    bool OnScalar(const std::string& value, const std::string& tag) override;
    bool OnSequenceStart() override;
    bool OnSequenceEnd() override;
    std::unique_ptr<Node> finish() override;
};

} // namespace AST
} // namespace AIML
