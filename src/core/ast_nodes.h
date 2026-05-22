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
    // 1-based line number in the AIML source file (-1 = unknown)
    int sourceLine = -1;
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

// dyn_array<T> heap allocation: { new_array: [ElemType, count] }
class NewArrayExpr : public Expr {
public:
    std::string elemTypeName;
    std::unique_ptr<Expr> countExpr;
    NewArrayExpr(std::string t) : elemTypeName(t) {}
    std::string toString() const override { return "NewArray(" + elemTypeName + ")"; }
};

class SpawnExpr : public Expr {
public:
    std::string functionName;
    std::vector<std::unique_ptr<Expr>> args;

    SpawnExpr(std::string name) : functionName(name) {}
    std::string toString() const override { return "Spawn(" + functionName + ")"; }
};

// Index expression: { at: [container, idx1, idx2, ...] }
// Works for array<T:N> (GEP), list<T> (bounds-checked get), tensor (tensor_get).
class AtExpr : public Expr {
public:
    std::unique_ptr<Expr> container;
    std::vector<std::unique_ptr<Expr>> indices;

    AtExpr() {}
    std::string toString() const override { return "At(...)"; }
};

// Sequence literal used as buffer<T> initializer: decl: [buf, buffer<f32>, [v1, v2, ...]]
// Compiler expands each element into a BufferImpl::write call.
class BufferLiteralExpr : public Expr {
public:
    std::vector<std::unique_ptr<Expr>> elements;
    BufferLiteralExpr() {}
    std::string toString() const override { return "BufferLiteral(...)"; }
};

// Inline option map: {key: value, ...} used as call argument (e.g. {context: true}).
// Values are kept as strings; callers parse booleans/numbers as needed.
class DictLiteralExpr : public Expr {
public:
    std::map<std::string, std::string> entries;
    explicit DictLiteralExpr(std::map<std::string, std::string> e) : entries(std::move(e)) {}
    std::string toString() const override { return "Dict(...)"; }
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
    bool is_extern = false;   // {extern: true} → cross-file ExternalLinkage (plain __lib_<name>)

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

// idx: [container, idx1, ..., idxN, value]  — indexed write
// Last arg is the value, first is container, rest are indices.
class IdxStmt : public Stmt {
public:
    std::string container;
    std::vector<std::unique_ptr<Expr>> indices;
    std::unique_ptr<Expr> value;

    IdxStmt(std::string c) : container(c) {}
    std::string toString() const override { return "Idx(" + container + "[...] = ...)"; }
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

// --- Extern "C" Declarations ---

struct ExternFuncDecl {
    std::string name;
    std::string returnType;
    std::vector<std::pair<std::string, std::string>> args; // name, type
};

class ExternBlock : public Node {
public:
    std::string abi;  // "C" (only supported ABI for now)
    std::vector<ExternFuncDecl> funcs;

    std::string toString() const override { return "ExternBlock(\"" + abi + "\")"; }
};

// --- Top-Level Declarations ---

class FunctionDecl : public Node {
public:
    std::string name;
    std::string returnType;
    uint64_t    version = 1;  // Hot-Reload version (default 1)
    std::vector<std::pair<std::string, std::string>> args; // name, type
    std::unique_ptr<BlockStmt> body;
    std::vector<std::string> staticOff; // disabled static checks, e.g. "lines"

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
    std::vector<std::string> syncFields;   // mirror: sync: [...]
    std::vector<std::string> rpcMethods;   // mirror: rpc:  [...]

    std::string toString() const override { return "ClassDecl(" + name + ")"; }
};

class ModelDecl : public Node {
public:
    std::string name;
    std::string parentName;   // for "inherits"
    uint64_t version = 1;
    std::string primaryKey;   // field name used as entity ID; empty = slot-index (i32)
    std::vector<StructMember> fields;
    std::vector<std::string> syncFields;   // mirror: sync: [...]
    std::vector<std::string> rpcMethods;   // mirror: rpc:  [...]

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

struct PrefabChildInfo {
    std::string name;
    std::string type;      // "sprite", "label", "rect", "button", "text_input"
    float x = 0, y = 0, width = 0, height = 0;
    float font_size = 12;
    uint32_t color = 0xFFFFFF;
    std::string texture;
    std::string anchor;    // "left", "center", "right"

    // UI-widget style (button, text_input, rect)
    uint32_t bg_color      = 0;
    uint32_t border_color  = 0;
    float    border_width  = 0.0f;
    float    corner_radius = 0.0f;
    uint32_t hover_color   = 0;
    uint32_t active_color  = 0;
    std::string label;        // button label / static label text
    bool     is_secret     = false; // text_input password mode

    // Callbacks (function names, resolved at runtime via fptable)
    std::string on_click;
    std::string on_change;

    // Material reference (looked up by name at instantiate time)
    std::string material;
};

struct MaterialUniformInfo {
    enum Kind { K_FLOAT, K_VEC2, K_VEC3, K_VEC4, K_INT, K_TEX } kind = K_FLOAT;
    float       scalar  = 0.0f;
    int32_t     integer = 0;
    std::vector<float> vec;   // 2..4 elements for vecN
    std::string tex_path;     // sampler2D
};

class MaterialDecl : public Node {
public:
    std::string name;
    std::string shader;       // path to fragment shader file
    std::vector<std::pair<std::string, MaterialUniformInfo>> uniforms;

    std::string toString() const override { return "MaterialDecl(" + name + ")"; }
};

class PrefabDecl : public Node {
public:
    std::string name;
    float width = 0, height = 0;
    std::string texture;
    bool draggable = false;
    std::vector<PrefabChildInfo> children;

    std::string toString() const override { return "PrefabDecl(" + name + ")"; }
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
