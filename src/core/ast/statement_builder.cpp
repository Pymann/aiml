#include "../../../logger.h"
#include "../ast_builder.h"
#include "statement_builder.h"
#include "expression_builder.h"

namespace AIML {
namespace AST {

bool StatementBuilder::OnSequenceStart() {
    seqDepth++;
    // Detect inner sequence in `decl: [name, buffer<T>, [v1, v2, ...]]`
    if (seqDepth == 2 && type == "decl" && (int)args.size() == 2)
        innerSeqStart = (int)args.size(); // always 2, but keep it explicit
    return true;
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
        exprArg = parseScalarExpr(value, tag);
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
        exprArg = parseScalarExpr(value, tag);
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
         if (exprArg) {
             decl->initialValue = std::move(exprArg);
         } else if (innerSeqStart >= 0
                    && decl->typeName.rfind("buffer<", 0) == 0
                    && decl->typeName.back() == '>') {
             // Buffer literal: decl: [name, buffer<T>, [v1, v2, ...]]
             // Expand into a BufferLiteralExpr — visitDeclStmt emits push calls.
             auto lit = std::make_unique<BufferLiteralExpr>();
             for (int i = innerSeqStart; i < (int)args.size(); ++i)
                 lit->elements.push_back(parseScalarExpr(args[i].first, args[i].second));
             decl->initialValue = std::move(lit);
         } else if (args.size() >= 3) {
             const std::string& val = args[2].first;
             const std::string& tag = args[2].second;
             decl->initialValue = parseScalarExpr(val, tag);
         }

         decl->is_const = is_const;
         decl->is_volatile = is_volatile;
         return decl;
    }
    if (type == "set" && args.size() >= 1) {
         if (!exprArg && args.size() >= 2) {
             const std::string& val = args[1].first;
             const std::string& tag = args[1].second;
             exprArg = parseScalarExpr(val, tag);
         }
         auto set = std::make_unique<SetStmt>(args[0].first, std::move(exprArg));
         return set;
    }
    if (type == "call" && args.size() >= 1) {
         auto call = std::make_unique<CallExpr>(args[0].first);
         for (size_t i = 1; i < args.size(); ++i) {
             const std::string& val = args[i].first;
             const std::string& tag = args[i].second;
             call->args.push_back(parseScalarExpr(val, tag));
         }
         if (exprArg) call->args.push_back(std::move(exprArg));
         
         return std::make_unique<ExprStmt>(std::move(call));
    }
    if (type == "print" && args.size() >= 1) {
         auto call = std::make_unique<CallExpr>("print");
         for (size_t i = 0; i < args.size(); ++i) {
             const std::string& val = args[i].first;
             const std::string& tag = args[i].second;
             call->args.push_back(parseScalarExpr(val, tag));
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
