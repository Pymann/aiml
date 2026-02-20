#pragma once
#include <memory>
#include <string>
#include <vector>
#include <yaml-cpp/yaml.h>
#include "ast_nodes.h"

namespace AIML {
namespace AST {

class Parser {
public:
    // Parse an entire AIML file (module/program)
    std::unique_ptr<Program> parseProgram(const YAML::Node& root);

    // Parse specific node types
    std::unique_ptr<Node> parseTopLevel(const YAML::Node& node);
    std::unique_ptr<Stmt> parseStatement(const YAML::Node& node);
    std::unique_ptr<Expr> parseExpression(const YAML::Node& node);

    // Error handling (simple for now, later track line numbers)
    struct ParseError : public std::runtime_error {
        ParseError(const std::string& msg, const YAML::Node& node) 
            : std::runtime_error("Parse error at line " + std::to_string(node.Mark().line) + ": " + msg) {}
    };

private:
    std::unique_ptr<FunctionDecl> parseFunction(const YAML::Node& node);
    std::unique_ptr<DeclStmt> parseDecl(const YAML::Node& node);
    std::unique_ptr<SetStmt> parseSet(const YAML::Node& node);
    std::unique_ptr<IfStmt> parseIf(const YAML::Node& node);
    
    // Expressions
    std::unique_ptr<Expr> parseStringExpansion(const std::string& str, const YAML::Node& refNode);
};

} // namespace AST
} // namespace AIML
