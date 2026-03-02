#include "../../../logger.h"
#include "../ast_builder.h"
#include "expression_builder.h"

namespace AIML {
namespace AST {

bool ExpressionBuilder::OnMapStart() {
    if (!op.empty()) {
        // We are already inside an operator like `{ add: [ { mul: [1, 2] }, 3 ] }`
        // We just encountered the inner map `{ mul: ... }`
        // Must initialise the inner ExpressionBuilder via its own OnMapStart so that
        // expectingKey = true is set, mirroring what StatementBuilder does.
        auto inner = std::make_unique<ExpressionBuilder>(builder);
        inner->OnMapStart();
        builder->pushState(std::move(inner));
        return true;
    }
    expectingKey = true;
    return true;
}

bool ExpressionBuilder::OnMapEnd() {
    builder->popState();
    return true;
}

bool ExpressionBuilder::OnScalar(const std::string& value, const std::string& tag) {
    if (expectingKey) {
        op = value;
        expectingKey = false;
        
        // Single-argument operations might just have a scalar value, e.g. `!true`
        // But usually it's `op: [args]`
        return true;
    }
    
    if (expectingArgs || (!op.empty() && !expectingArgs)) {
        if (tag == "!") {
            if (value.find("${") != std::string::npos) args.push_back(parseInterpolatedString(value));
            else args.push_back(std::make_unique<LiteralExpr>(value, "string"));
        } else if (value == "true" || value == "false" || isdigit(value[0]) || (value.length() > 0 && value[0] == '-')) {
             args.push_back(std::make_unique<LiteralExpr>(value, "unknown"));
        } else {
             args.push_back(std::make_unique<VariableExpr>(value));
        }
        return true;
    }
    
    return true;
}

bool ExpressionBuilder::OnSequenceStart() {
    if (!op.empty() && !expectingArgs) {
        expectingArgs = true;
        return true;
    }
    return false;
}

bool ExpressionBuilder::OnSequenceEnd() {
    expectingArgs = false;
    return true;
}

void ExpressionBuilder::receiveChildNode(std::unique_ptr<Node> node) {
    if (auto e = dynamic_cast<Expr*>(node.release())) {
        args.push_back(std::unique_ptr<Expr>(e));
    }
}

std::unique_ptr<Node> ExpressionBuilder::finish() {
    if (op == "call" && !args.empty()) {
         auto callName = dynamic_cast<VariableExpr*>(args[0].get());
         auto callLiteral = dynamic_cast<LiteralExpr*>(args[0].get());
         std::string name = "unknown_func";
         if (callName) name = callName->name;
         else if (callLiteral) name = callLiteral->value;
         
         auto call = std::make_unique<CallExpr>(name);
         for (size_t i = 1; i < args.size(); ++i) {
             call->args.push_back(std::move(args[i]));
         }
         return call;
    }
    if (op == "spawn" && !args.empty()) {
         auto spawnName = dynamic_cast<VariableExpr*>(args[0].get());
         auto spawnLiteral = dynamic_cast<LiteralExpr*>(args[0].get());
         std::string name = "unknown_func";
         if (spawnName) name = spawnName->name;
         else if (spawnLiteral) name = spawnLiteral->value;

         auto spawnExpr = std::make_unique<SpawnExpr>(name);
         for (size_t i = 1; i < args.size(); ++i)
             spawnExpr->args.push_back(std::move(args[i]));
         return spawnExpr;
    }
    if (op == "new" && !args.empty()) {
         auto newName = dynamic_cast<VariableExpr*>(args[0].get());
         auto newLiteral = dynamic_cast<LiteralExpr*>(args[0].get());
         std::string name = "unknown_class";
         if (newName) name = newName->name;
         else if (newLiteral) name = newLiteral->value;
         
         auto newExpr = std::make_unique<NewExpr>(name);
         for (size_t i = 1; i + 1 < args.size(); i += 2) {
             auto keyExpr = dynamic_cast<VariableExpr*>(args[i].get());
             if (keyExpr) {
                 newExpr->initValues[keyExpr->name] = std::move(args[i+1]);
             }
         }
         return newExpr;
    }
    if (args.size() == 2) {
         return std::make_unique<BinaryExpr>(op, std::move(args[0]), std::move(args[1]));
    }
    // Unary operators (single argument)
    if (args.size() == 1) {
        return std::make_unique<UnaryExpr>(op, std::move(args[0]));
    }
    return nullptr;
}

} // namespace AST
} // namespace AIML
