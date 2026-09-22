#include "../../../logger.h"
#include "../ast_builder.h"
#include "expression_builder.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace {
// Normalize symbol operators to their canonical word forms.
// This is the single source of truth for operator spelling.
static const std::unordered_map<std::string, std::string> kOpAlias = {
    {"<",  "lt"}, {">",  "gt"},
    {"<=", "lte"}, {">=", "gte"},
};
static const std::unordered_map<std::string, std::string> kDeprecatedOps = {
    {"==", "eq"}, {"!=", "neq"}, {"<>", "neq"},
};
inline std::string normalizeOp(const std::string& op) {
    auto it = kOpAlias.find(op);
    return (it != kOpAlias.end()) ? it->second : op;
}

bool isKnownBinaryOperator(const std::string& op) {
    static const std::unordered_set<std::string> kBinaryOps = {
        "+", "-", "*", "/", "%", "add", "sub", "mul", "div",
        "eq", "neq", "lt", "gt", "le", "lte", "ge", "gte", "peq", "pneq",
        "and", "&&", "or", "||"
    };
    return kBinaryOps.count(op) > 0;
}

bool isVariadicFoldBinaryOperator(const std::string& op) {
    return op == "+" || op == "*" || op == "add" || op == "mul";
}

bool isAllOf(const std::string& op, char c) {
    return !op.empty() && std::all_of(op.begin(), op.end(), [c](char ch) { return ch == c; });
}

bool isStrictUnaryOnlyOperator(const std::string& op) {
    if (op == "!" || op == "&") return true;
    if (isAllOf(op, '?')) return true;
    if (op.size() > 2 && isAllOf(op, '&')) return true;
    return false;
}

std::unique_ptr<AIML::AST::Expr> foldBinaryExprList(
    std::string op, std::vector<std::unique_ptr<AIML::AST::Expr>> args) {
    auto acc = std::make_unique<AIML::AST::BinaryExpr>(op, std::move(args[0]), std::move(args[1]));
    for (size_t i = 2; i < args.size(); ++i) {
        acc = std::make_unique<AIML::AST::BinaryExpr>(op, std::move(acc), std::move(args[i]));
    }
    return acc;
}
} // anonymous namespace

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
        // Reject deprecated symbol operators
        auto dep = kDeprecatedOps.find(value);
        if (dep != kDeprecatedOps.end()) {
            if (builder->currentLine > 0)
                LOG_ERROR("line " + std::to_string(builder->currentLine) + ": Operator '" + value + "' is not allowed. Use '" + dep->second + "' instead.");
            else
                LOG_ERROR("Operator '" + value + "' is not allowed. Use '" + dep->second + "' instead.");
            return false;
        }
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
        } else if (value == "null") {
            args.push_back(std::make_unique<LiteralExpr>("null", "null"));
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
        hadSequence = true;
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
    // tolerance(a, b, eps) → ternary operator, emitted as CallExpr
    if (op == "tolerance" && args.size() == 3) {
         auto call = std::make_unique<CallExpr>("__builtin_tolerance");
         call->args.push_back(std::move(args[0]));
         call->args.push_back(std::move(args[1]));
         call->args.push_back(std::move(args[2]));
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
    if (op == "at" && !args.empty()) {
         auto atExpr = std::make_unique<AtExpr>();
         atExpr->container = std::move(args[0]);
         // args[1..]: leading INDICES, then optional trailing DOT-PREFIXED member names —
         // { at: [c, i, .field, .f2] } = c[i].field.f2 (dash-free `c-i.field.f2`). A leading '.'
         // is unambiguous (indices/variables never start with one) and needs no quotes, so it is
         // safe inside string interpolation.
         auto memberName = [](Expr* e) -> std::string {
             if (auto* v = dynamic_cast<VariableExpr*>(e)) if (!v->name.empty() && v->name[0]=='.') return v->name.substr(1);
             if (auto* l = dynamic_cast<LiteralExpr*>(e))  if (!l->value.empty() && l->value[0]=='.') return l->value.substr(1);
             return std::string();
         };
         bool inMembers = false;
         for (size_t i = 1; i < args.size(); ++i) {
             std::string mn = memberName(args[i].get());
             if (!mn.empty()) inMembers = true;
             if (inMembers) { if (!mn.empty()) atExpr->memberPath.push_back(mn); }
             else atExpr->indices.push_back(std::move(args[i]));
         }
         return atExpr;
    }
    if (op == "ref" && args.size() == 1) {
         // spec/61 §6.2: { ref: x } is an explicit non-owning borrow of the lvalue `x`. The
         // target `ref<T>` slot/parameter drives the borrow (a ref<T> local bound from `x`
         // already takes the borrow, verified), so lowering the wrapper to `x` is sufficient.
         return std::move(args[0]);
    }
    if (op == "share" && args.size() == 1) {
         // spec/61 §6.2: { share: E } yields a shared<T> co-owned handle. The target `shared<T>`
         // slot drives the %M* allocation ({ new: T } under a shared target allocs a refcounted
         // block) / adoption, so lowering the wrapper to E is sufficient for the fresh-new case.
         return std::move(args[0]);
    }
    if (op == "clone" && args.size() == 1) {
         // spec/61 §5.1: { clone: x } is a deep (memoized, graph-preserving) copy.
         auto call = std::make_unique<CallExpr>("sys.clone");
         call->args.push_back(std::move(args[0]));
         return call;
    }
    if (op == "copy" && args.size() == 1) {
         // spec/61 §5.1: { copy: x } is a shallow copy (referents shared).
         auto call = std::make_unique<CallExpr>("sys.copy");
         call->args.push_back(std::move(args[0]));
         return call;
    }
    if (op == "slice" && args.size() >= 3) {
         // { slice: [container, start, end] } → CallExpr("__builtin_slice", container, start, end)
         auto call = std::make_unique<CallExpr>("__builtin_slice");
         for (auto& a : args)
             call->args.push_back(std::move(a));
         return call;
    }
    if (op == "new_array" && args.size() == 2) {
        auto typeArg  = dynamic_cast<VariableExpr*>(args[0].get());
        auto typeLit  = dynamic_cast<LiteralExpr*>(args[0].get());
        std::string elemType = "i32";
        if (typeArg)  elemType = typeArg->name;
        else if (typeLit) elemType = typeLit->value;
        auto na = std::make_unique<NewArrayExpr>(elemType);
        na->countExpr = std::move(args[1]);
        return na;
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
    if (hadSequence) {
        std::string normalizedOp = normalizeOp(op);
        if (args.size() > 1 && isStrictUnaryOnlyOperator(op)) {
            if (builder->currentLine > 0) {
                LOG_ERROR("line " + std::to_string(builder->currentLine) + ": Unary operator '" + op +
                          "' expects exactly 1 operand, got " + std::to_string(args.size()) + ".");
            } else {
                LOG_ERROR("Unary operator '" + op + "' expects exactly 1 operand, got " +
                          std::to_string(args.size()) + ".");
            }
            return nullptr;
        }
        if (args.size() > 2 && isKnownBinaryOperator(normalizedOp)) {
            if (isVariadicFoldBinaryOperator(normalizedOp)) {
                return foldBinaryExprList(normalizedOp, std::move(args));
            }
            if (builder->currentLine > 0) {
                LOG_ERROR("line " + std::to_string(builder->currentLine) + ": Binary operator '" + normalizedOp +
                          "' expects exactly 2 operands, got " + std::to_string(args.size()) + ".");
            } else {
                LOG_ERROR("Binary operator '" + normalizedOp + "' expects exactly 2 operands, got " +
                          std::to_string(args.size()) + ".");
            }
            return nullptr;
        }
        if (args.size() == 2) {
            return std::make_unique<BinaryExpr>(normalizedOp, std::move(args[0]), std::move(args[1]));
        }
        if (args.size() == 1) {
            return std::make_unique<UnaryExpr>(op, std::move(args[0]));
        }
        return nullptr;
    }

    // No sequence: dict-style map {key1: val1, key2: val2, ...}
    // op = first key, args = [val1, key2, val2, key3, val3, ...]
    if (!op.empty() && !args.empty()) {
        auto getStr = [](Expr* e) -> std::string {
            if (auto* lit = dynamic_cast<LiteralExpr*>(e)) return lit->value;
            if (auto* var = dynamic_cast<VariableExpr*>(e)) return var->name;
            return "";
        };
        std::map<std::string, std::string> entries;
        std::vector<std::pair<std::string, std::unique_ptr<Expr>>> entriesExpr;
        std::string firstVal = getStr(args[0].get());
        entries[op] = firstVal;
        entriesExpr.emplace_back(op, std::move(args[0]));
        bool ok = true;
        for (size_t i = 1; i + 1 < args.size(); i += 2) {
            std::string keyName;
            if (auto* v = dynamic_cast<VariableExpr*>(args[i].get())) {
                keyName = v->name;
            } else if (auto* lit = dynamic_cast<LiteralExpr*>(args[i].get())) {
                keyName = lit->value;
            } else {
                ok = false; break;
            }
            entries[keyName] = getStr(args[i+1].get());
            entriesExpr.emplace_back(keyName, std::move(args[i+1]));
        }
        if (ok && !entries.empty()) {
            auto d = std::make_unique<DictLiteralExpr>(std::move(entries));
            d->entriesExpr = std::move(entriesExpr);
            return d;
        }
    }

    // Unary/binary fallback for args that didn't come via sequence
    std::string normalizedOp = normalizeOp(op);
    if (args.size() > 2 && isKnownBinaryOperator(normalizedOp)) {
        if (isVariadicFoldBinaryOperator(normalizedOp)) {
            return foldBinaryExprList(normalizedOp, std::move(args));
        }
        if (builder->currentLine > 0) {
            LOG_ERROR("line " + std::to_string(builder->currentLine) + ": Binary operator '" + normalizedOp +
                      "' expects exactly 2 operands, got " + std::to_string(args.size()) + ".");
        } else {
            LOG_ERROR("Binary operator '" + normalizedOp + "' expects exactly 2 operands, got " +
                      std::to_string(args.size()) + ".");
        }
        return nullptr;
    }
    if (args.size() == 2) {
         return std::make_unique<BinaryExpr>(normalizedOp, std::move(args[0]), std::move(args[1]));
    }
    if (args.size() == 1) {
        return std::make_unique<UnaryExpr>(op, std::move(args[0]));
    }
    return nullptr;
}

} // namespace AST
} // namespace AIML
