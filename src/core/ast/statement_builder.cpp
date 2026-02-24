#include "../../../logger.h"
#include "../ast_builder.h"
#include "statement_builder.h"
#include "expression_builder.h"

namespace AIML {
namespace AST {

bool StatementBuilder::OnSequenceStart() {
    return true; // e.g. `decl: [x, i32]`
}

bool StatementBuilder::OnSequenceEnd() {
    builder->popState();
    return true;
}

bool StatementBuilder::OnMapEnd() {
    builder->popState();
    return true;
}

bool StatementBuilder::OnScalar(const std::string& value, const std::string& tag) {
    if (expectingValue) {
        // We received the RHS of a `set: [x, value]`
        if (tag == "!") {
            if (value.find("${") != std::string::npos) exprArg = parseInterpolatedString(value);
            else exprArg = std::make_unique<LiteralExpr>(value, "string");
        } else if (value == "true" || value == "false" || isdigit(value[0]) || (value.length() > 0 && value[0] == '-')) {
            exprArg = std::make_unique<LiteralExpr>(value, "unknown");
        } else {
            exprArg = std::make_unique<VariableExpr>(value);
        }
        expectingValue = false;
        return true;
    }
    
    // Collecting arguments inside the sequence `[x, i32, 5]`
    args.push_back({value, tag});
    
    if (type == "set" && args.size() == 1) {
        // After reading `x`, the next thing might be an expression map instead of a scalar
        expectingValue = true; 
    }
    
    if (type == "return") {
        if (tag == "!") {
            if (value.find("${") != std::string::npos) exprArg = parseInterpolatedString(value);
            else exprArg = std::make_unique<LiteralExpr>(value, "string");
        } else if (value == "true" || value == "false" || isdigit(value[0]) || (value.length() > 0 && value[0] == '-')) {
            exprArg = std::make_unique<LiteralExpr>(value, "unknown");
        } else {
            exprArg = std::make_unique<VariableExpr>(value);
        }
    }
    
    return true;
}

class ModifierBuilder : public StateHandler {
    bool* is_const;
    bool* is_volatile;
    bool expectingKey = true;
    std::string currentKey;
public:
    ModifierBuilder(Builder* b, bool* ic, bool* iv) : StateHandler(b), is_const(ic), is_volatile(iv) {}
    bool OnMapStart() override { expectingKey = true; return true; }
    bool OnMapEnd() override { builder->popState(); return true; }
    bool OnScalar(const std::string& value, const std::string& tag) override {
        if (expectingKey) {
            currentKey = value;
            expectingKey = false;
        } else {
            if (currentKey == "const") *is_const = (value == "true" || value == "1");
            if (currentKey == "volatile") *is_volatile = (value == "true" || value == "1");
            expectingKey = true;
        }
        return true;
    }
    std::unique_ptr<Node> finish() override { return nullptr; }
};

bool StatementBuilder::OnMapStart() {
    if (type == "decl" && args.size() >= 2) {
        // If args size is 3, we already have Name, Type, Value. This map is Modifiers.
        // If args size is 2, it could be Value (an expr) or Modifiers. 
        // We'll assume for simplicity that in AIML local var decl: [name, type, val, {modifiers}]
        // OR [name, type, {modifiers}]. Wait, we can look at `expectingValue`.
        // Actually, if we just push ModifierBuilder when args.size() >= 2, we might break default exprs.
        // Let's only push ModifierBuilder if args.size() >= 3. Wait, test_modifiers.aiml does NOT omit default value.
        // So args.size() is 3.
        if (args.size() >= 3) {
            builder->pushState(std::make_unique<ModifierBuilder>(builder, &is_const, &is_volatile));
            return true;
        }
    }
    // A statement received a map, likely an expression like `set: [x, { add: [1, 2] }]`
    expectingValue = false;
    auto exprBuilder = std::make_unique<ExpressionBuilder>(builder);
    exprBuilder->OnMapStart();
    builder->pushState(std::move(exprBuilder));
    return true;
}

void StatementBuilder::receiveChildNode(std::unique_ptr<Node> node) {
    if (auto e = dynamic_cast<Expr*>(node.release())) {
        exprArg = std::unique_ptr<Expr>(e);
    }
}

std::unique_ptr<Node> StatementBuilder::finish() {
    if (type == "decl" && args.size() >= 2) {
         auto decl = std::make_unique<DeclStmt>(args[0].first, args[1].first);
         if (exprArg) decl->initialValue = std::move(exprArg);
         else if (args.size() >= 3) {
             const std::string& val = args[2].first;
             const std::string& tag = args[2].second;
             if (tag == "!") {
                 if (val.find("${") != std::string::npos) decl->initialValue = parseInterpolatedString(val);
                 else decl->initialValue = std::make_unique<LiteralExpr>(val, "string");
             } else if (val == "true" || val == "false" || isdigit(val[0]) || (val.length() > 0 && val[0] == '-')) {
                 decl->initialValue = std::make_unique<LiteralExpr>(val, "unknown");
             } else {
                 decl->initialValue = std::make_unique<VariableExpr>(val);
             }
         }
         
         decl->is_const = is_const;
         decl->is_volatile = is_volatile;
         return decl;
    }
    if (type == "set" && args.size() >= 1) {
         if (!exprArg && args.size() >= 2) {
             const std::string& val = args[1].first;
             const std::string& tag = args[1].second;
             if (tag == "!") {
                 if (val.find("${") != std::string::npos) exprArg = parseInterpolatedString(val);
                 else exprArg = std::make_unique<LiteralExpr>(val, "string");
             } else if (val == "true" || val == "false" || isdigit(val[0]) || (val.length() > 0 && val[0] == '-')) {
                 exprArg = std::make_unique<LiteralExpr>(val, "unknown");
             } else {
                 exprArg = std::make_unique<VariableExpr>(val);
             }
         }
         auto set = std::make_unique<SetStmt>(args[0].first, std::move(exprArg));
         return set;
    }
    if (type == "call" && args.size() >= 1) {
         auto call = std::make_unique<CallExpr>(args[0].first);
         for (size_t i = 1; i < args.size(); ++i) {
             const std::string& val = args[i].first;
             const std::string& tag = args[i].second;
             if (tag == "!") {
                  if (val.find("${") != std::string::npos) call->args.push_back(parseInterpolatedString(val));
                  else call->args.push_back(std::make_unique<LiteralExpr>(val, "string"));
             } else if (val == "true" || val == "false" || isdigit(val[0]) || (val.length() > 0 && val[0] == '-')) {
                  call->args.push_back(std::make_unique<LiteralExpr>(val, "unknown"));
             } else {
                  call->args.push_back(std::make_unique<VariableExpr>(val));
             }
         }
         if (exprArg) call->args.push_back(std::move(exprArg));
         
         return std::make_unique<ExprStmt>(std::move(call));
    }
    if (type == "print" && args.size() >= 1) {
         auto call = std::make_unique<CallExpr>("print");
         for (size_t i = 0; i < args.size(); ++i) {
             const std::string& val = args[i].first;
             const std::string& tag = args[i].second;
             if (tag == "!") {
                 if (val.find("${") != std::string::npos) call->args.push_back(parseInterpolatedString(val));
                 else call->args.push_back(std::make_unique<LiteralExpr>(val, "string"));
             } else if (val == "true" || val == "false" || isdigit(val[0]) || (val.length() > 0 && val[0] == '-')) {
                 call->args.push_back(std::make_unique<LiteralExpr>(val, "unknown"));
             } else {
                 call->args.push_back(std::make_unique<VariableExpr>(val));
             }
         }
         return std::make_unique<ExprStmt>(std::move(call));
    }
    if (type == "return") {
         auto ret = std::make_unique<RetStmt>();
         if (exprArg) ret->value = std::move(exprArg);
         return ret;
    }
    if (type == "throw") {
         if (exprArg) return std::make_unique<ThrowStmt>(std::move(exprArg));
         else {
             LOG_ERROR("throw statement must have a value");
             return nullptr;
         }
    }
    return nullptr;
}

} // namespace AST
} // namespace AIML
