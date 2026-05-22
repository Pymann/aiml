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

class ModelBuilder : public StateHandler {
    std::unique_ptr<ModelDecl> modelNode;
    std::string currentKey;
    bool expectingKey = false;
    bool insideFields = false;
    bool insideSync   = false;
    bool insideRpc    = false;
    int mapDepth = 0; // Start at 0

    // State for parsing member sequence entry
    std::string currentField;
    std::string currentType;
    int memberAttrIndex = 0; // [name, type, default]
    std::unique_ptr<Expr> currentInitValue;
    std::string currentAccess = "public";
public:
    ModelBuilder(Builder* b) : StateHandler(b) { modelNode = std::make_unique<ModelDecl>(); }
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
