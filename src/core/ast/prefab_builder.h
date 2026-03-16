#pragma once
#include <memory>
#include <string>
#include <vector>
#include "../ast_nodes.h"

namespace AIML {
namespace AST {

class Builder;
class StateHandler;

class PrefabBuilder : public StateHandler {
    std::unique_ptr<PrefabDecl> prefabNode;
    std::string currentKey;
    bool expectingKey = false;
    bool insideChildren = false;
    bool insideChildObj = false;
    int mapDepth = 0;

    // Current child being parsed
    PrefabChildInfo currentChild;
    std::string childKey;
    bool childExpectingKey = false;

public:
    PrefabBuilder(Builder* b) : StateHandler(b) { prefabNode = std::make_unique<PrefabDecl>(); }
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
