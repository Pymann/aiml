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
    // Zahl der WIRKLICH gesehenen Skalare.  `args.size()` taugt dafuer nicht: der
    // expectingValue-Zweig in OnScalar kehrt frueh zurueck, bevor er in args landet.
    int scalarCount = 0;
    // spec/10: ein Listenliteral als Aufrufargument.  Ohne das wandert jedes Element
    // einzeln in orderedCallArgs und wird so zu mehreren Argumenten aufgefaechert
    // (Backlog-Punkt 3).  Waehrend der inneren Sequenz sammeln wir hier.
    std::unique_ptr<BufferLiteralExpr> pendingCallLiteral;
    // spec/10: ein Element eines Listen-/Buffer-Literals darf eine Expression sein
    // (`decl: [tags, list<string>, ["P", { call: ... }]]`).  Eine Map-Expression waechst
    // `args` NICHT, also haelt dieser Vektor sie mit der Stelle fest, an der sie stand:
    // first = args.size() im Moment ihres Eintreffens.  Ohne das fiel sie still weg.
    std::vector<std::pair<int, std::unique_ptr<Expr>>> innerSeqExprs;
    bool is_const = false;
    bool is_singleton = false;   // spec/21 §6.2 F21-S1
    bool is_volatile = false;
    bool is_extern = false;
    std::string decl_lifetime = "scope";   // {lifetime: scope|persistent|consumed} (spec/61)
    int decl_level = -1;                   // {level: N} (spec/65 F65-19); -1 = absent
    int seqDepth = 0;       // nesting depth of sequences seen
    bool retSeq = false;    // `return: [a, b]` -- der Wert war eine Folge (spec/10)
    int innerSeqStart = -1; // args[] index where inner sequence elements begin
    int capturedLine = -1;  // 1-based line of this statement in the AIML source
    int capturedColumn = -1;  // dazugehoerige Spalte (spec/49 F49-100)
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
    // Baut das Literal aus args[from..] und flicht die Expressions an ihrer Stelle ein.
    std::unique_ptr<BufferLiteralExpr> buildSeqLiteral(int from);
    // Sind wir gerade INNERHALB der inneren Sequenz eines Literals?  Dann ist eine Map ein
    // ELEMENT und kein Modifier-Block.
    // Bei `decl`/`set` steckt das Literal EINE Ebene tiefer, bei `return` ist es die
    // Folge selbst -- sonst waere eine Ausdrucks-Map darin kein Element (spec/10).
    bool insideSeqLiteral() const { return (innerSeqStart >= 0 && seqDepth >= 2) || (retSeq && seqDepth >= 1); }
};

} // namespace AST
} // namespace AIML
