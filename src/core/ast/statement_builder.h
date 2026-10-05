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
    std::vector<std::unique_ptr<Expr>> exprArgs; // for call: multiple nested expr args
    std::vector<std::unique_ptr<Expr>> orderedCallArgs; // call args in source order (scalars + exprs interleaved)
    // idx: everything after the container in source order — a ".field" name or an expression.
    std::vector<std::pair<std::string, std::unique_ptr<Expr>>> idxParts;
    bool expectingValue = false;
    bool is_const = false;
    bool is_volatile = false;
    bool is_extern = false;
    std::string decl_lifetime = "scope";   // {lifetime: scope|persistent|consumed} (spec/61)
    int seqDepth = 0;       // nesting depth of sequences seen
    int innerSeqStart = -1; // args[] index where inner sequence elements begin
    int capturedLine = -1;  // 1-based line of this statement in the AIML source
public:
    StatementBuilder(Builder* b, std::string t) : StateHandler(b), type(t), capturedLine(b->currentLine) {}
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
