#include "ast_parser.h"
#include <iostream>

namespace AIML {
namespace AST {

std::unique_ptr<Program> Parser::parseProgram(const YAML::Node& root) {
    auto prog = std::make_unique<Program>();
    prog->sourceNode = root;
    
    if (root.IsSequence()) {
        for (const auto& node : root) {
            auto child = parseTopLevel(node);
            if (child) {
                prog->declarations.push_back(std::move(child));
            }
        }
    }
    return prog;
}

std::unique_ptr<Node> Parser::parseTopLevel(const YAML::Node& node) {
    if (!node.IsMap()) return nullptr;
    
    if (node["def"]) {
        return parseFunction(node["def"]);
    }
    
    // Ignore directives we don't compile to IR directly yet
    if (node["import"] || node["namespace"] || node["config"]) {
        return nullptr;
    }
    
    // TODO: class, model, interop, etc.
    
    // Fallback: maybe it's a global statement?
    return parseStatement(node);
}

std::unique_ptr<FunctionDecl> Parser::parseFunction(const YAML::Node& node) {
    auto func = std::make_unique<FunctionDecl>();
    func->sourceNode = node;
    
    if (node["name"].IsScalar()) {
        func->name = node["name"].as<std::string>();
    } else {
        throw ParseError("Function missing name", node);
    }
    
    if (node["return"].IsScalar()) {
        func->returnType = node["return"].as<std::string>();
    } else {
        func->returnType = "void"; // Default
    }
    
    if (node["args"].IsSequence()) {
        for (const auto& argNode : node["args"]) {
            if (argNode.IsSequence() && argNode.size() >= 2) {
                func->args.push_back({argNode[0].as<std::string>(), argNode[1].as<std::string>()});
            }
        }
    }
    
    if (node["body"].IsSequence()) {
        func->body = std::make_unique<BlockStmt>();
        func->body->sourceNode = node["body"];
        for (const auto& stmtNode : node["body"]) {
            auto stmt = parseStatement(stmtNode);
            if (stmt) {
                func->body->statements.push_back(std::move(stmt));
            }
        }
    }
    
    return func;
}

std::unique_ptr<Stmt> Parser::parseStatement(const YAML::Node& node) {
    if (!node.IsMap()) return nullptr;
    
    if (node["decl"]) return parseDecl(node["decl"]);
    if (node["set"]) return parseSet(node["set"]);
    if (node["if"]) return parseIf(node["if"]);
    if (node["call"]) {
        auto exprStmt = std::make_unique<ExprStmt>(parseExpression(node));
        exprStmt->sourceNode = node;
        return exprStmt;
    }
    
    // Unknown statement
    return nullptr;
}

std::unique_ptr<DeclStmt> Parser::parseDecl(const YAML::Node& node) {
    if (node.IsSequence() && node.size() >= 2) {
        auto decl = std::make_unique<DeclStmt>(node[0].as<std::string>(), node[1].as<std::string>());
        decl->sourceNode = node;
        if (node.size() >= 3) {
            decl->initialValue = parseExpression(node[2]);
        }
        return decl;
    } else if (node.IsMap()) {
        if (!node["name"].IsScalar() || !node["type"].IsScalar()) {
             throw ParseError("Map decl missing name or type", node);
        }
        auto decl = std::make_unique<DeclStmt>(node["name"].as<std::string>(), node["type"].as<std::string>());
        decl->sourceNode = node;
        if (node["value"]) {
             decl->initialValue = parseExpression(node["value"]);
        }
        return decl;
    }
    throw ParseError("Invalid decl syntax", node);
}

std::unique_ptr<SetStmt> Parser::parseSet(const YAML::Node& node) {
    if (node.IsSequence() && node.size() >= 2) {
        auto setStmt = std::make_unique<SetStmt>(node[0].as<std::string>(), parseExpression(node[1]));
        setStmt->sourceNode = node;
        return setStmt;
    }
    throw ParseError("Invalid set syntax", node);
}

std::unique_ptr<IfStmt> Parser::parseIf(const YAML::Node& node) {
    auto ifStmt = std::make_unique<IfStmt>();
    ifStmt->sourceNode = node;
    // TODO: implement full feature parsing
    return ifStmt;
}

std::unique_ptr<Expr> Parser::parseExpression(const YAML::Node& node) {
    if (node.IsScalar()) {
        std::string val = node.as<std::string>();
        // Very basic heuristic for string vs variable/number for prototype
        if (!val.empty() && (val[0] == '"' || val.find("${") != std::string::npos)) {
             return std::make_unique<LiteralExpr>(val, "string");
        }
        if (val == "true" || val == "false") return std::make_unique<LiteralExpr>(val, "bool");
        if (isdigit(val[0])) return std::make_unique<LiteralExpr>(val, "i32");
        return std::make_unique<VariableExpr>(val);
    }
    if (node.IsMap()) {
        if (node["call"]) {
            auto callArgs = node["call"];
            if (callArgs.IsSequence() && callArgs.size() > 0) {
                auto callExpr = std::make_unique<CallExpr>(callArgs[0].as<std::string>());
                for (size_t i = 1; i < callArgs.size(); ++i) {
                    callExpr->args.push_back(parseExpression(callArgs[i]));
                }
                return callExpr;
            }
        }
        // Operators like { add: [a, b] }
        if (node["add"]) {
            auto args = node["add"];
            return std::make_unique<BinaryExpr>("add", parseExpression(args[0]), parseExpression(args[1]));
        }
    }
    return nullptr;
}

} // namespace AST
} // namespace AIML
