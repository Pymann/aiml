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

class BlockBuilder : public StateHandler {
    std::unique_ptr<BlockStmt> block;
    // True once a map has started for the current statement item.  A block is a
    // sequence of statement MAPS; a scalar item (e.g. a bare `- return` instead
    // of `- { return }`) arrives with no preceding OnMapStart → invalid.
    bool inStatementMap = false;
public:
    BlockBuilder(Builder* b) : StateHandler(b) { block = std::make_unique<BlockStmt>(); }
    bool OnSequenceStart() override;
    bool OnSequenceEnd() override;
    bool OnMapStart() override;
    bool OnMapEnd() override;
    bool OnScalar(const std::string& value, const std::string& tag) override;
    void receiveChildNode(std::unique_ptr<Node> node) override;
    std::unique_ptr<Node> finish() override;
};

} // namespace AST
} // namespace AIML
