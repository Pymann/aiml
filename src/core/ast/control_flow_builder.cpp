#include "../../../logger.h"
#include "../ast_builder.h"
#include "control_flow_builder.h"
#include "expression_builder.h"
#include "block_builder.h"

namespace AIML {
namespace AST {

bool ControlFlowBuilder::OnMapStart() {
    if (!expectingKey && (currentKey == "cond" || currentKey == "condition" || type == "ret")) {
        auto exprBuilder = std::make_unique<ExpressionBuilder>(builder);
        exprBuilder->OnMapStart();
        builder->pushState(std::move(exprBuilder));
        return true;
    }
    expectingKey = true;
    return true; // We are inside e.g. `if: { cond: ..., then: ... }`
}

bool ControlFlowBuilder::OnMapEnd() {
    builder->popState();
    return true;
}

bool ControlFlowBuilder::OnSequenceStart() {
    if (type == "ret") {
        builder->pushState(std::make_unique<ExpressionBuilder>(builder));
        return true;
    }
    
    // An array inside an if/while map is typically a block (e.g., `then: [...]`)
    if (currentKey == "then" || currentKey == "else" || currentKey == "body") {
        builder->pushState(std::make_unique<BlockBuilder>(builder));
        return true;
    }
    
    // Could also be an expression `cond: { eq: [...] }` but the ExpressionBuilder handles its own sequence.
    // If we reach here, maybe `cond` itself is a direct array? Push expression builder.
    auto exprBuilder = std::make_unique<ExpressionBuilder>(builder);
    if (!expectingKey && (currentKey == "cond" || currentKey == "condition")) {
        exprBuilder->OnMapStart();
    }
    builder->pushState(std::move(exprBuilder));
    return true;
}

bool ControlFlowBuilder::OnScalar(const std::string& value, const std::string& tag) {
    if (expectingKey) {
        currentKey = value;
        expectingKey = false;
        return true;
    }
    
    // We are expecting a value for `cond:` or `ret:`
    // Example: `ret: 1` or `cond: true`
    if (!expectingKey) {
        if (currentKey == "cond" || currentKey == "condition" || type == "ret") {
             if (tag == "!") {
                 if (value.find("${") != std::string::npos) condition = parseInterpolatedString(value);
                 else condition = std::make_unique<LiteralExpr>(value, "string");
             } else if (value == "true" || value == "false" || isdigit(value[0]) || (value.length() > 0 && value[0] == '-')) {
                 condition = std::make_unique<LiteralExpr>(value, "unknown");
             } else {
                 condition = std::make_unique<VariableExpr>(value);
             }
        }
        expectingKey = true;
        return true;
    }
    return false;
}

void ControlFlowBuilder::receiveChildNode(std::unique_ptr<Node> node) {
    if (auto e = dynamic_cast<Expr*>(node.get())) {
        e = dynamic_cast<Expr*>(node.release());
        if (type == "ret") {
            condition = std::unique_ptr<Expr>(e);
        } else if (currentKey == "cond" || currentKey == "condition") {
            condition = std::unique_ptr<Expr>(e);
        }
    } else if (auto b = dynamic_cast<BlockStmt*>(node.get())) {
        b = dynamic_cast<BlockStmt*>(node.release());
        if (currentKey == "then") thenBranch = std::unique_ptr<BlockStmt>(b);
        else if (currentKey == "else") elseBranch = std::unique_ptr<BlockStmt>(b);
        else if (currentKey == "body") body = std::unique_ptr<BlockStmt>(b);
    }
    expectingKey = true; // Prepare for next key
}

std::unique_ptr<Node> ControlFlowBuilder::finish() {
    if (type == "if") {
        auto ifStmt = std::make_unique<IfStmt>();
        ifStmt->condition = std::move(condition);
        ifStmt->thenBranch = std::move(thenBranch);
        ifStmt->elseBranch = std::move(elseBranch);
        return ifStmt;
    } else if (type == "while") {
        auto whileStmt = std::make_unique<WhileStmt>();
        whileStmt->condition = std::move(condition);
        whileStmt->body = std::move(body);
        return whileStmt;
    } else if (type == "ret") {
        auto retStmt = std::make_unique<RetStmt>();
        retStmt->value = std::move(condition);
        return retStmt;
    }
    return nullptr;
}

} // namespace AST
} // namespace AIML
