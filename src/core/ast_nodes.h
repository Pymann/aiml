#pragma once
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <yaml-cpp/yaml.h>

namespace AIML {
namespace AST {

// Base class for all AST nodes
class Node {
public:
    virtual ~Node() = default;
    
    // For debugging/printing the tree
    virtual std::string toString() const = 0;
    
    // Original YAML node for exact line numbers/error reporting
    YAML::Node sourceNode; 
};

// --- Expressions (Evaluate to a value) ---

class Expr : public Node {
public:
    virtual ~Expr() = default;
};

class LiteralExpr : public Expr {
public:
    std::string value; // e.g. "42", "true", "\"hello\""
    std::string type;  // e.g. "i32", "bool", "string"

    LiteralExpr(std::string v, std::string t) : value(v), type(t) {}
    std::string toString() const override { return "Literal(" + type + ": " + value + ")"; }
};

class VariableExpr : public Expr {
public:
    std::string name;

    VariableExpr(std::string n) : name(n) {}
    std::string toString() const override { return "Variable(" + name + ")"; }
};

class BinaryExpr : public Expr {
public:
    std::string op;
    std::unique_ptr<Expr> lhs;
    std::unique_ptr<Expr> rhs;

    BinaryExpr(std::string op, std::unique_ptr<Expr> l, std::unique_ptr<Expr> r)
        : op(op), lhs(std::move(l)), rhs(std::move(r)) {}
    std::string toString() const override { return "Binary(" + op + ", " + lhs->toString() + ", " + rhs->toString() + ")"; }
};

// Unary operators: '?' (deref), '??' (double-deref), '&' (addr-of), '&&' (addr-of-addr), '!' (logical not)
class UnaryExpr : public Expr {
public:
    std::string op;
    std::unique_ptr<Expr> operand;

    UnaryExpr(std::string op, std::unique_ptr<Expr> operand)
        : op(op), operand(std::move(operand)) {}
    std::string toString() const override { return "Unary(" + op + ", " + operand->toString() + ")"; }
};

class CallExpr : public Expr {
public:
    std::string functionName;
    std::vector<std::unique_ptr<Expr>> args;

    CallExpr(std::string name) : functionName(name) {}
    std::string toString() const override { return "Call(" + functionName + ")"; }
};

class StringInterpolationExpr : public Expr {
public:
    std::vector<std::unique_ptr<Expr>> parts;

    std::string toString() const override { return "StringInterpolation(...)"; }
};

class NewExpr : public Expr {
public:
    std::string typeName;
    std::map<std::string, std::unique_ptr<Expr>> initValues;

    NewExpr(std::string tName) : typeName(tName) {}
    std::string toString() const override { return "New(" + typeName + ")"; }
};

// --- Statements (Perform an action) ---

class Stmt : public Node {
public:
    virtual ~Stmt() = default;
};

class BlockStmt : public Stmt {
public:
    std::vector<std::unique_ptr<Stmt>> statements;

    std::string toString() const override { return "Block(...)"; }
};

class DeclStmt : public Stmt {
public:
    std::string varName;
    std::string typeName;
    std::unique_ptr<Expr> initialValue;
    bool is_const = false;
    bool is_volatile = false;

    DeclStmt(std::string n, std::string t) : varName(n), typeName(t) {}
    std::string toString() const override { return "Decl(" + typeName + " " + varName + ")"; }
};

class SetStmt : public Stmt {
public:
    std::string target; // Simple variable for now, later LHS expression
    std::unique_ptr<Expr> value;

    SetStmt(std::string t, std::unique_ptr<Expr> v) : target(t), value(std::move(v)) {}
    std::string toString() const override { return "Set(" + target + " = ...)"; }
};

class ExprStmt : public Stmt {
public:
    std::unique_ptr<Expr> expr;

    ExprStmt(std::unique_ptr<Expr> e) : expr(std::move(e)) {}
    std::string toString() const override { return "ExprStmt(" + expr->toString() + ")"; }
};

class IfStmt : public Stmt {
public:
    std::unique_ptr<Expr> condition;
    std::unique_ptr<BlockStmt> thenBranch;
    std::unique_ptr<BlockStmt> elseBranch; // Optional

    std::string toString() const override { return "If(...)"; }
};

class WhileStmt : public Stmt {
public:
    std::unique_ptr<Expr> condition;
    std::unique_ptr<BlockStmt> body;

    std::string toString() const override { return "While(...)"; }
};

class RetStmt : public Stmt {
public:
    std::unique_ptr<Expr> value; // Optional

    std::string toString() const override { return "Ret(...)"; }
};

class ThrowStmt : public Stmt {
public:
    std::unique_ptr<Expr> value;

    ThrowStmt(std::unique_ptr<Expr> v) : value(std::move(v)) {}
    std::string toString() const override { return "Throw(" + value->toString() + ")"; }
};

class TryStmt : public Stmt {
public:
    std::unique_ptr<BlockStmt> tryBlock;
    std::unique_ptr<BlockStmt> catchBlock;
    std::string catchVar;
    std::string catchType;

    std::string toString() const override { return "Try(...) Catch(...)"; }
};

// --- Top-Level Declarations ---

class FunctionDecl : public Node {
public:
    std::string name;
    std::string returnType;
    std::vector<std::pair<std::string, std::string>> args; // name, type
    std::unique_ptr<BlockStmt> body;

    std::string toString() const override { return "FunctionDecl(" + name + ")"; }
};

struct StructMember {
    std::string name;
    std::string type;
    std::unique_ptr<Expr> initialValue; // Applicable for classes/models
    uint64_t version = 1;
    std::string access = "public";
    bool is_const = false;
    bool is_volatile = false;
};

struct MethodSignature {
    std::string name;
    std::string returnType;
    std::vector<std::pair<std::string, std::string>> args;
};

class ClassDecl : public Node {
public:
    std::string name;
    std::string parentName; // for "extends"
    uint64_t version = 1;
    std::vector<std::string> models;
    std::vector<std::string> interfaces;
    std::vector<StructMember> members;
    std::vector<std::unique_ptr<FunctionDecl>> methods;

    std::string toString() const override { return "ClassDecl(" + name + ")"; }
};

class ModelDecl : public Node {
public:
    std::string name;
    uint64_t version = 1;
    std::vector<StructMember> fields;
    
    std::string toString() const override { return "ModelDecl(" + name + ")"; }
};

class InterfaceDecl : public Node {
public:
    std::string name;
    std::vector<MethodSignature> methods;

    std::string toString() const override { return "InterfaceDecl(" + name + ")"; }
};

class EnumDecl : public Node {
public:
    std::string name;
    std::vector<std::pair<std::string, int>> values; // enum name, integer value

    std::string toString() const override { return "EnumDecl(" + name + ")"; }
};

// --- Modular Declarations ---

class NamespaceDecl : public Node {
public:
    std::string name;
    std::string toString() const override { return "NamespaceDecl(" + name + ")"; }
};

class ImportDecl : public Node {
public:
    std::string path;
    std::string toString() const override { return "ImportDecl(" + path + ")"; }
};

class ConfigDecl : public Node {
public:
    std::string entryPoint;
    size_t stackSize = 0;
    size_t heapSize = 0;
    std::string target;
    std::string version;
    std::string toString() const override { return "ConfigDecl(...)"; }
};

class Program : public Node {
public:
    std::vector<std::unique_ptr<Node>> declarations; // Functions, Classes, Global Stmts

    std::string toString() const override { return "Program(...)"; }
};

} // namespace AST
} // namespace AIML
