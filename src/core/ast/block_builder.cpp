#include "../../../logger.h"
#include "../ast_builder.h"
#include "block_builder.h"
#include "control_flow_builder.h"
#include "try_builder.h"
#include "statement_builder.h"

namespace AIML {
namespace AST {

// ── TextBuilder ─────────────────────────────────────────────────────────────
// Handles: - text: <varname>
//            value: |
//              multiline content...
// Produces a DeclStmt(varname, "string", LiteralExpr(content, "string")).
class TextBuilder : public StateHandler {
    std::string varName;
    std::string content;
    int scalarCount = 0; // 0=varname, 1="value" key (ignored), 2=content
public:
    TextBuilder(Builder* b) : StateHandler(b) {}
    bool OnScalar(const std::string& value, const std::string&) override {
        if      (scalarCount == 0) varName = value;
        else if (scalarCount == 2) content = value;
        ++scalarCount;
        return true;
    }
    bool OnMapEnd() override { builder->popState(); return true; }
    std::unique_ptr<Node> finish() override {
        auto decl = std::make_unique<DeclStmt>(varName, "string");
        decl->initialValue = std::make_unique<LiteralExpr>(content, "string");
        return decl;
    }
};

bool BlockBuilder::OnSequenceStart() {
    return true; // The block itself is a sequence
}

bool BlockBuilder::OnSequenceEnd() {
    builder->popState();
    return true;
}

bool BlockBuilder::OnMapStart() {
    return true;
}

bool BlockBuilder::OnMapEnd() {
    return true; 
}

bool BlockBuilder::OnScalar(const std::string& value, const std::string& tag) {
    if (value == "if" || value == "while" || value == "ret") {
        builder->pushState(std::make_unique<ControlFlowBuilder>(builder, value));
    } else if (value == "try") {
        builder->pushState(std::make_unique<TryBuilder>(builder));
    } else if (value == "text") {
        builder->pushState(std::make_unique<TextBuilder>(builder));
    } else {
        builder->pushState(std::make_unique<StatementBuilder>(builder, value));
    }
    return true;
}

void BlockBuilder::receiveChildNode(std::unique_ptr<Node> node) {
    if (auto stmt = dynamic_cast<Stmt*>(node.release())) {
        block->statements.push_back(std::unique_ptr<Stmt>(stmt));
    }
}

std::unique_ptr<Node> BlockBuilder::finish() {
    return std::move(block);
}

} // namespace AST
} // namespace AIML
