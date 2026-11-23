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
    // Number of scalars REALLY seen.  `args.size()` is no good for that: the
    // expectingValue branch in OnScalar returns early, before it lands in args.
    int scalarCount = 0;
    // spec/10: a list literal as a call argument.  Without this every element moves
    // individually into orderedCallArgs and is thus fanned out into several arguments
    // (backlog item 3).  During the inner sequence we collect here.
    std::unique_ptr<BufferLiteralExpr> pendingCallLiteral;
    // spec/10: an element of a list/buffer literal may be an expression
    // (`decl: [tags, list<string>, ["P", { call: ... }]]`).  A map expression does NOT grow
    // `args`, so this vector keeps it together with the position where it stood:
    // first = args.size() at the moment it arrived.  Without that it silently fell away.
    std::vector<std::pair<int, std::unique_ptr<Expr>>> innerSeqExprs;
    bool is_const = false;
    bool is_singleton = false;   // spec/21 §6.2 F21-S1
    bool is_volatile = false;
    bool is_extern = false;
    std::string decl_lifetime = "scope";   // {lifetime: scope|persistent|consumed} (spec/61)
    int decl_level = -1;                   // {level: N} (spec/65 F65-19); -1 = absent
    int seqDepth = 0;       // nesting depth of sequences seen
    bool retSeq = false;    // `return: [a, b]` -- the value was a sequence (spec/10)
    int innerSeqStart = -1; // args[] index where inner sequence elements begin
    int capturedLine = -1;  // 1-based line of this statement in the AIML source
    int capturedColumn = -1;  // the associated column (spec/49 F49-100)
public:
    StatementBuilder(Builder* b, std::string t) : StateHandler(b), type(t), capturedLine(b->currentLine), capturedColumn(b->currentColumn) {}
    bool OnSequenceStart() override;
    bool OnSequenceEnd() override;
    bool OnScalar(const std::string& value, const std::string& tag) override;
    bool OnMapStart() override;
    bool OnMapEnd() override;
    void receiveChildNode(std::unique_ptr<Node> node) override;
    std::unique_ptr<Node> finish() override;
private:
    // Builds the literal from args[from..] and weaves the expressions in at their position.
    std::unique_ptr<BufferLiteralExpr> buildSeqLiteral(int from);
    // Are we currently INSIDE the inner sequence of a literal?  Then a map is an
    // ELEMENT and not a modifier block.
    // For `decl`/`set` the literal sits ONE level deeper, for `return` it is the
    // sequence itself -- otherwise an expression map in it would not be an element (spec/10).
    bool insideSeqLiteral() const { return (innerSeqStart >= 0 && seqDepth >= 2) || (retSeq && seqDepth >= 1); }
};

} // namespace AST
} // namespace AIML
