#include "../../../logger.h"
#include "../ast_builder.h"
#include "block_builder.h"
#include "control_flow_builder.h"
#include "try_builder.h"
#include "statement_builder.h"
#include "text_builder.h"

namespace AIML {
namespace AST {

bool BlockBuilder::OnSequenceStart() {
    return true; // The block itself is a sequence
}

bool BlockBuilder::OnSequenceEnd() {
    builder->popState();
    return true;
}

bool BlockBuilder::OnMapStart() {
    inStatementMap = true;
    return true;
}

bool BlockBuilder::OnMapEnd() {
    inStatementMap = false;
    return true;
}

bool BlockBuilder::OnScalar(const std::string& value, const std::string& tag) {
    // A statement item must be a MAP (`- { call: [...] }`, `- return:`, ...).
    // A BARE SCALAR item (`- return`, `- foo`) reaches OnScalar with no preceding
    // OnMapStart.  Previously this silently spawned a dangling StatementBuilder
    // that swallowed the following YAML events (dropping later statements AND
    // even later top-level defs) with no diagnostic.  Reject it loudly instead.
    if (!inStatementMap) {
        LOG_ERROR("invalid statement: bare scalar '" + value +
                  "' in a statement block — a statement must be a map, e.g. write "
                  "`- { " + value + " }` or `- " + value + ":` (with a value) "
                  "instead of `- " + value + "`.");
        return true;  // swallow the scalar; do NOT spawn a builder that eats the rest
    }
    inStatementMap = false;  // consume: the map's key has been read
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
