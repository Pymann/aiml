#define DEBUG_MODE
#include "ast_builder.h"
#include <cstring>
#include "ast/root_state.h"
#include "ast/statement_builder.h"
#include "ast/block_builder.h"
#include "ast/class_builder.h"
#include "ast/model_builder.h"
#include "ast/interface_builder.h"
#include "ast/enum_builder.h"
#include "ast/namespace_builder.h"
#include "ast/import_builder.h"
#include "ast/config_builder.h"
#include "ast/function_builder.h"
#include "ast/control_flow_builder.h"
#include "ast/expression_builder.h"
#include "ast/try_builder.h"
#include "../../logger.h"
#include <iostream>
#include <yaml-cpp/yaml.h>
#include <algorithm>
#include <unordered_set>

namespace {
bool isAllOf(const std::string& op, char c) {
    return !op.empty() && std::all_of(op.begin(), op.end(), [c](char ch) { return ch == c; });
}

bool isStrictUnaryOnlyOperator(const std::string& op) {
    if (op == "!" || op == "not" || op == "&") return true;
    if (isAllOf(op, '?')) return true;
    // && is ambiguous because it is also the binary logical-and spelling.
    if (op.size() > 2 && isAllOf(op, '&')) return true;
    return false;
}

bool isKnownBinaryOperator(const std::string& op) {
    static const std::unordered_set<std::string> kBinaryOps = {
        "+", "-", "*", "/", "%", "add", "sub", "mul", "div",
        "eq", "neq", "lt", "gt", "le", "lte", "ge", "gte", "peq", "pneq",
        "and", "&&", "or", "||", "equiv", "xor"
    };
    return kBinaryOps.count(op) > 0;
}

bool isVariadicFoldBinaryOperator(const std::string& op) {
    return op == "+" || op == "*" || op == "add" || op == "mul";
}
}

namespace AIML {
namespace AST {

std::unique_ptr<Expr> parseInterpolatedString(const std::string& input);

thread_local int g_builderLine = 0;

std::string bareOperatorKey(const std::string& text) {
    // Lange Formen zuerst, sonst schluckt `>` das `>=`.
    static const char* kReserved[] = {">=", ">>", "||", "&&", "!=",
                                      "*", "%", ">", "|", "?", "&", "!", "~", "@", "`"};
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '{' && text[i] != ',') continue;
        size_t a = text.find_first_not_of(" \t", i + 1);
        if (a == std::string::npos) break;
        for (const char* op : kReserved) {
            size_t n = std::strlen(op);
            if (text.compare(a, n, op) != 0) continue;
            size_t b = text.find_first_not_of(" \t", a + n);
            if (b != std::string::npos && text[b] == ':') return op;
        }
    }
    return "";
}

std::unique_ptr<Expr> nodeToExpr(const YAML::Node& node) {
    if (node.Mark().line >= 0) g_builderLine = node.Mark().line + 1;
    if (node.IsScalar()) {
        std::string val = node.as<std::string>();
        if (node.Tag() == "!") {
            if (val.find("${") != std::string::npos) return parseInterpolatedString(val);
            return std::make_unique<LiteralExpr>(val, "string");
        }
        if (val == "true" || val == "false" || (val.length() > 0 && isdigit(val[0])) || (val.length() > 0 && val[0] == '-')) {
            return std::make_unique<LiteralExpr>(val, "unknown");
        }
        return std::make_unique<VariableExpr>(val);
    }
    if (node.IsMap()) {
        if (node["new"]) {
            std::string className;
            if (node["new"].IsScalar()) className = node["new"].as<std::string>();
            else if (node["new"].IsMap() && node["new"].begin()->first.as<std::string>() == "new") {} // Ignore
            
            auto newExpr = std::make_unique<NewExpr>(className);
            for (auto it = node.begin(); it != node.end(); ++it) {
                std::string key = it->first.as<std::string>();
                if (key != "new") {
                    newExpr->initValues[key] = nodeToExpr(it->second);
                }
            }
            return newExpr;
        }

        std::string op = node.begin()->first.as<std::string>();
        auto argsNode = node.begin()->second;
        LOG_DEBUG("nodeToExpr: Map op=" + op + " isSeq=" + std::to_string(argsNode.IsSequence()) + " size=" + std::to_string(argsNode.size()));
        
        if (op == "call" && argsNode.IsSequence() && argsNode.size() > 0) {
            std::string funcName;
            if (argsNode[0].IsScalar()) funcName = argsNode[0].as<std::string>();
            else if (argsNode[0].IsMap() && argsNode[0].begin()->first.as<std::string>() == "call") {
                // Should not happen as function name
            }
            auto callExpr = std::make_unique<CallExpr>(funcName);
            callExpr->sourceNode = node; // preserve YAML Mark for source location
            for(size_t i=1; i<argsNode.size(); ++i) {
                callExpr->args.push_back(nodeToExpr(argsNode[i]));
            }
            return callExpr;
        }

        if (op == "spawn" && argsNode.IsSequence() && argsNode.size() >= 1) {
            std::string funcName = argsNode[0].IsScalar() ? argsNode[0].as<std::string>() : "unknown";
            auto spawnExpr = std::make_unique<SpawnExpr>(funcName);
            for (size_t i = 1; i < argsNode.size(); ++i)
                spawnExpr->args.push_back(nodeToExpr(argsNode[i]));
            return spawnExpr;
        }
        
        // spec/61 §5.1: { clone: x } / { copy: x } — deep / shallow copy.
        if ((op == "clone" || op == "copy") && !argsNode.IsSequence()) {
            auto call = std::make_unique<CallExpr>(op == "clone" ? "sys.clone" : "sys.copy");
            call->sourceNode = node;
            call->args.push_back(nodeToExpr(argsNode));
            return call;
        }
        if ((op == "clone" || op == "copy") && argsNode.IsSequence() && argsNode.size() == 1) {
            auto call = std::make_unique<CallExpr>(op == "clone" ? "sys.clone" : "sys.copy");
            call->sourceNode = node;
            call->args.push_back(nodeToExpr(argsNode[0]));
            return call;
        }

        // { at: [container, index..., .member...] } — element access with an optional dot-prefixed
        // trailing member path (the dash-free `c-i.field` form, also usable in interpolation).
        if (op == "at" && argsNode.IsSequence() && argsNode.size() >= 1) {
            auto atExpr = std::make_unique<AtExpr>();
            atExpr->sourceNode = node;
            atExpr->container = nodeToExpr(argsNode[0]);
            bool inMembers = false;
            for (size_t i = 1; i < argsNode.size(); ++i) {
                std::string mn;
                if (argsNode[i].IsScalar()) {
                    std::string s = argsNode[i].as<std::string>();
                    if (!s.empty() && s[0] == '.') mn = s.substr(1);
                }
                if (!mn.empty()) inMembers = true;
                if (inMembers) { if (!mn.empty()) atExpr->memberPath.push_back(mn); }
                else atExpr->indices.push_back(nodeToExpr(argsNode[i]));
            }
            return atExpr;
        }

        // Unary operators: single-argument maps like { '?': [p] }, { '&': [x] }
        if (argsNode.IsSequence() && argsNode.size() == 1) {
            return std::make_unique<UnaryExpr>(op, nodeToExpr(argsNode[0]));
        }

        if (argsNode.IsSequence() && argsNode.size() > 1 && isStrictUnaryOnlyOperator(op)) {
            size_t line = node.Mark().line + 1;
            LOG_ERROR("line " + std::to_string(line) + ": Unary operator '" + op +
                      "' expects exactly 1 operand, got " + std::to_string(argsNode.size()) + ".");
            return nullptr;
        }

        // Binary operators
        if (argsNode.IsSequence() && argsNode.size() == 2) {
             return std::make_unique<BinaryExpr>(op, nodeToExpr(argsNode[0]), nodeToExpr(argsNode[1]));
        }
        if (argsNode.IsSequence() && argsNode.size() > 2 && isKnownBinaryOperator(op)) {
            size_t line = node.Mark().line + 1;
            if (isVariadicFoldBinaryOperator(op)) {
                auto acc = std::make_unique<BinaryExpr>(op, nodeToExpr(argsNode[0]), nodeToExpr(argsNode[1]));
                for (size_t i = 2; i < argsNode.size(); ++i) {
                    acc = std::make_unique<BinaryExpr>(op, std::move(acc), nodeToExpr(argsNode[i]));
                }
                return acc;
            }
            LOG_ERROR("line " + std::to_string(line) + ": Binary operator '" + op +
                      "' expects exactly 2 operands, got " + std::to_string(argsNode.size()) + ".");
            return nullptr;
        }

        // Unrecognised map (e.g. option bags like {context: true}) → DictLiteralExpr.
        // Iterate all keys; values are coerced to string.
        {
            std::map<std::string, std::string> entries;
            for (auto it = node.begin(); it != node.end(); ++it) {
                try {
                    entries[it->first.as<std::string>()] = it->second.as<std::string>();
                } catch (...) {}
            }
            if (!entries.empty())
                return std::make_unique<DictLiteralExpr>(std::move(entries));
        }
    }
    return nullptr;
}

std::unique_ptr<Expr> parseScalarExpr(const std::string& val, const std::string& tag) {
    if (tag == "!") {
        if (val.find("${") != std::string::npos) return parseInterpolatedString(val);
        else return std::make_unique<LiteralExpr>(val, "string");
    } else if (val == "null" || tag == "!!null") {
        return std::make_unique<LiteralExpr>("null", "null");
    } else if (val == "true" || val == "false" || isdigit(val[0]) || (val.length() > 0 && val[0] == '-')) {
        return std::make_unique<LiteralExpr>(val, "unknown");
    } else if (val.find("${") != std::string::npos) {
        return parseInterpolatedString(val);
    } else if (val.length() > 0 && (val[0] == '"' || val[0] == '\'')) {
        return std::make_unique<LiteralExpr>(val, "string");
    } else {
        return std::make_unique<VariableExpr>(val);
    }
}

std::unique_ptr<Expr> parseInterpolatedString(const std::string& input) {
    if (input.find("${") == std::string::npos) {
        return std::make_unique<LiteralExpr>(input, "string");
    }
    
    auto interp = std::make_unique<StringInterpolationExpr>();
    // Der Fundort gilt fuer den GANZEN Skalar.  Er muss vor der Schleife festgehalten
    // werden: `nodeToExpr` laeuft unten auf dem Mini-Dokument der Expansion und setzt
    // g_builderLine aus DESSEN Marke -- dort ist alles Zeile 1.
    const int zeile = g_builderLine;
    size_t pos = 0;
    while (pos < input.length()) {
        size_t start = input.find("${", pos);
        if (start == std::string::npos) {
            std::string suffix = input.substr(pos);
            if (!suffix.empty()) interp->parts.push_back(std::make_unique<LiteralExpr>(suffix, "string"));
            break;
        }
        
        if (start > pos) {
            interp->parts.push_back(std::make_unique<LiteralExpr>(input.substr(pos, start - pos), "string"));
        }
        
        size_t end = std::string::npos;
        int braceDepth = 0;
        for (size_t i = start + 2; i < input.length(); ++i) {
             if (input[i] == '{') braceDepth++;
             else if (input[i] == '}') {
                 if (braceDepth > 0) braceDepth--;
                 else {
                      end = i;
                      break;
                 }
             }
        }
        if (end == std::string::npos) break; // Malformed
        
        std::string exprStr = input.substr(start + 2, end - start - 2);
        // spec/09_1 §3.5: laesst sich der Koerper nicht als Ausdruck lesen, bleibt die
        // Expansion wortwoertlicher Text ("so schreibt man ${...}" ist Prosa und soll das
        // bleiben).  Genau dieser Rueckfall verschluckte aber den blanken
        // Operatorschluessel: `${ { %: [lauf, 40] } }` ist kein gueltiges YAML, `Load`
        // warf, und in die Zeichenkette kam der Text -- Austritt 0, keine Meldung.
        // Gemessen 06.11.2026 an 300.000 Zeilen, die alle denselben "Wert" trugen.
        // Die Erklaerung haengt darum AM FEHLSCHLAG, nicht am Koerper: Code, der heute
        // laeuft, kommt hier nie vorbei.
        auto alsText = [&](const char* grund) {
            std::string blank = bareOperatorKey(exprStr);
            if (!blank.empty()) {
                std::string wo = zeile > 0 ? ("line " + std::to_string(zeile) + ": ") : "";
                size_t sa = exprStr.find_first_not_of(" \t");
                size_t se = exprStr.find_last_not_of(" \t");
                std::string knapp = (sa == std::string::npos) ? exprStr
                                                              : exprStr.substr(sa, se - sa + 1);
                LOG_ERROR(wo + "operator key '" + blank + "' in an expansion must be quoted"
                          " -- a bare '" + blank + "' is a YAML indicator character, so ${ "
                          + knapp + " } " + grund + " and was silently left as text."
                          " Write ${ { \\\"" + blank + "\\\": [...] } } (spec/09_1 §3.5).");
            }
            interp->parts.push_back(std::make_unique<LiteralExpr>("${" + exprStr + "}", "string"));
        };
        try {
            YAML::Node node = YAML::Load(exprStr);
            LOG_DEBUG("parseInterpolatedString: Loaded YAML node for: " + exprStr);
            auto astExpr = nodeToExpr(node);
            if (astExpr) {
                LOG_DEBUG("parseInterpolatedString: Successfully created AST expr for: " + exprStr);
                interp->parts.push_back(std::move(astExpr));
            } else {
                alsText("is not an expression");
            }
        } catch (...) {
            alsText("is not valid YAML");
        }
        g_builderLine = zeile;
        
        pos = end + 1;
    }
    
    return interp;
}

Builder::Builder() {
    program = std::make_unique<Program>();
    stateStack.push(std::make_unique<RootState>(this));
}

std::unique_ptr<Program> Builder::takeProgram() {
    return std::move(program);
}

void Builder::addDeclaration(std::unique_ptr<Node> node) {
    if (program && node) {
        program->declarations.push_back(std::move(node));
    }
}

void Builder::pushState(std::unique_ptr<StateHandler> state) {
    LOG_DEBUG("Pushing new state into Builder stack.");
    stateStack.push(std::move(state));
}

void Builder::popState() {
    if (stateStack.size() > 1) {
        auto finishedState = std::move(stateStack.top());
        stateStack.pop();
        
        LOG_DEBUG("Popping state in Builder");
        auto finishedNode = finishedState->finish();
        if (finishedNode) {
            LOG_DEBUG("Passing finishedNode to parent state.");
            stateStack.top()->receiveChildNode(std::move(finishedNode));
        } else {
            LOG_DEBUG("Finished state returned nullptr.");
        }
    }
}

// Event Router
void Builder::OnNull(const YAML::Mark&, YAML::anchor_t) {
    LOG_DEBUG("Router OnNull");
    stateStack.top()->OnScalar("null", "null");
}

void Builder::OnScalar(const YAML::Mark& mark, const std::string& tag, YAML::anchor_t, const std::string& value) {
    currentLine = mark.line + 1; // YAML marks are 0-based; store 1-based
    g_builderLine = currentLine;
    LOG_DEBUG("Router OnScalar: " + value);
    stateStack.top()->OnScalar(value, tag);
}

void Builder::OnSequenceStart(const YAML::Mark& mark, const std::string&, YAML::anchor_t, YAML::EmitterStyle::value) {
    currentLine = mark.line + 1;
    g_builderLine = currentLine;
    LOG_DEBUG("Router OnSequenceStart");
    stateStack.top()->OnSequenceStart();
}

void Builder::OnSequenceEnd() {
    LOG_DEBUG("Router OnSequenceEnd");
    stateStack.top()->OnSequenceEnd();
}

void Builder::OnMapStart(const YAML::Mark& mark, const std::string&, YAML::anchor_t, YAML::EmitterStyle::value) {
    currentLine = mark.line + 1;
    g_builderLine = currentLine;
    LOG_DEBUG("Router OnMapStart");
    stateStack.top()->OnMapStart();
}

void Builder::OnMapEnd() {
    LOG_DEBUG("Router OnMapEnd");
    stateStack.top()->OnMapEnd();
}


} // namespace AST
} // namespace AIML
