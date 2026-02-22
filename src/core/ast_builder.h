#pragma once
#include <memory>
#include <string>
#include <vector>
#include <stack>
#include <yaml-cpp/yaml.h>
#include <yaml-cpp/eventhandler.h>
#include "ast_nodes.h"

namespace AIML {
namespace AST {

class Builder;

// Base class for object-oriented states in the streaming parser
class StateHandler {
protected:
    Builder* builder;
public:
    StateHandler(Builder* b) : builder(b) {}
    virtual ~StateHandler() = default;

    // Return true if the child handled it, false if it's invalid
    virtual bool OnScalar(const std::string& value, const std::string& tag) { return false; }
    virtual bool OnSequenceStart() { return false; }
    virtual bool OnSequenceEnd() { return false; }
    virtual bool OnMapStart() { return false; }
    virtual bool OnMapEnd() { return false; }
    
    // Called by the Builder when a child state finishes and returns a built node
    virtual void receiveChildNode(std::unique_ptr<Node> node) {}
    
    // Called when this state handler is popped off the stack
    virtual std::unique_ptr<Node> finish() { return nullptr; }
};

class Builder : public YAML::EventHandler {
public:
    Builder();
    std::unique_ptr<Program> takeProgram();
    void addDeclaration(std::unique_ptr<Node> node);

    void pushState(std::unique_ptr<StateHandler> state);
    void popState();

    // YAML::EventHandler Overrides
    void OnDocumentStart(const YAML::Mark& mark) override {}
    void OnDocumentEnd() override {}
    void OnNull(const YAML::Mark& mark, YAML::anchor_t anchor) override {}
    void OnAlias(const YAML::Mark& mark, YAML::anchor_t anchor) override {}
    void OnScalar(const YAML::Mark& mark, const std::string& tag, YAML::anchor_t anchor, const std::string& value) override;
    void OnSequenceStart(const YAML::Mark& mark, const std::string& tag, YAML::anchor_t anchor, YAML::EmitterStyle::value style) override;
    void OnSequenceEnd() override;
    void OnMapStart(const YAML::Mark& mark, const std::string& tag, YAML::anchor_t anchor, YAML::EmitterStyle::value style) override;
    void OnMapEnd() override;

private:
    std::unique_ptr<Program> program;
    std::stack<std::unique_ptr<StateHandler>> stateStack;
};

// --- Specific State Handlers ---

class RootState : public StateHandler {
    bool expectingKey = false;
    std::string currentKey = "";
public:
    RootState(Builder* b) : StateHandler(b) {}
    bool OnMapStart() override;
    bool OnMapEnd() override;
    bool OnScalar(const std::string& value, const std::string& tag) override;
    void receiveChildNode(std::unique_ptr<Node> node) override;
};

// Parses a single statement like `decl: [x, i32]` or `set: [x, 5]`
class StatementBuilder : public StateHandler {
    std::string type;
    std::vector<std::pair<std::string, std::string>> args;
    std::unique_ptr<Expr> exprArg;
    bool expectingValue = false;
public:
    StatementBuilder(Builder* b, std::string t) : StateHandler(b), type(t) {}
    bool OnSequenceStart() override;
    bool OnSequenceEnd() override;
    bool OnScalar(const std::string& value, const std::string& tag) override;
    bool OnMapStart() override;
    bool OnMapEnd() override;
    void receiveChildNode(std::unique_ptr<Node> node) override;
    std::unique_ptr<Node> finish() override;
};

// Parses a sequence of statements
class BlockBuilder : public StateHandler {
    std::unique_ptr<BlockStmt> block;
public:
    BlockBuilder(Builder* b) : StateHandler(b) { block = std::make_unique<BlockStmt>(); }
    bool OnSequenceStart() override;
    bool OnSequenceEnd() override;
    bool OnMapStart() override;
    bool OnMapEnd() override;
    bool OnScalar(const std::string& value, const std::string& tag) override;
    void receiveChildNode(std::unique_ptr<Node> node) override;
    std::unique_ptr<Node> finish() override;
};

// Parses a `class:` node
class ClassBuilder : public StateHandler {
    std::unique_ptr<ClassDecl> classNode;
    std::string currentKey;
    bool expectingKey = false;
    bool insideMembers = false;
    bool insideMethods = false;
    
    // State for parsing member sequence entry
    std::string currentField;
    std::string currentType;
public:
    ClassBuilder(Builder* b) : StateHandler(b) { classNode = std::make_unique<ClassDecl>(); }
    bool OnMapStart() override;
    bool OnMapEnd() override;
    bool OnScalar(const std::string& value, const std::string& tag) override;
    bool OnSequenceStart() override;
    bool OnSequenceEnd() override;
    void receiveChildNode(std::unique_ptr<Node> node) override;
    std::unique_ptr<Node> finish() override;
};

// Parses a `model:` node
class ModelBuilder : public StateHandler {
    std::unique_ptr<ModelDecl> modelNode;
    std::string currentKey;
    bool expectingKey = false;
    bool insideFields = false;
    
    // State for parsing member sequence entry
    std::string currentField;
    std::string currentType;
public:
    ModelBuilder(Builder* b) : StateHandler(b) { modelNode = std::make_unique<ModelDecl>(); }
    bool OnMapStart() override;
    bool OnMapEnd() override;
    bool OnScalar(const std::string& value, const std::string& tag) override;
    bool OnSequenceStart() override;
    bool OnSequenceEnd() override;
    void receiveChildNode(std::unique_ptr<Node> node) override;
    std::unique_ptr<Node> finish() override;
};

// Parses an `interface:` node
class InterfaceBuilder : public StateHandler {
    std::unique_ptr<InterfaceDecl> interfaceNode;
    std::string currentKey;
    bool expectingKey = false;
    bool insideMethods = false;
    
    bool insideMethodDef = false;
    MethodSignature currentMethod;
public:
    InterfaceBuilder(Builder* b) : StateHandler(b) { interfaceNode = std::make_unique<InterfaceDecl>(); }
    bool OnMapStart() override;
    bool OnMapEnd() override;
    bool OnScalar(const std::string& value, const std::string& tag) override;
    bool OnSequenceStart() override;
    bool OnSequenceEnd() override;
    std::unique_ptr<Node> finish() override;
};

// Parses an `enum:` node
class EnumBuilder : public StateHandler {
    std::unique_ptr<EnumDecl> enumNode;
    std::string currentKey;
    bool expectingKey = false;
    bool insideValues = false;
    int overrideValue = -1;
    std::string activeValueName;
public:
    EnumBuilder(Builder* b) : StateHandler(b) { enumNode = std::make_unique<EnumDecl>(); }
    bool OnMapStart() override;
    bool OnMapEnd() override;
    bool OnScalar(const std::string& value, const std::string& tag) override;
    bool OnSequenceStart() override;
    bool OnSequenceEnd() override;
    std::unique_ptr<Node> finish() override;
};

// Parses a `namespace:` node (string value)
class NamespaceBuilder : public StateHandler {
    std::unique_ptr<NamespaceDecl> nsNode;
    std::string currentKey;
    bool expectingKey = false;
public:
    NamespaceBuilder(Builder* b) : StateHandler(b) { nsNode = std::make_unique<NamespaceDecl>(); }
    bool OnMapStart() override { expectingKey = true; return true; }
    bool OnMapEnd() override { builder->popState(); return true; }
    bool OnScalar(const std::string& value, const std::string& tag) override;
    std::unique_ptr<Node> finish() override;
};

// Parses an `import:` node (string value)
class ImportBuilder : public StateHandler {
    std::unique_ptr<ImportDecl> importNode;
    std::string currentKey;
    bool expectingKey = false;
public:
    ImportBuilder(Builder* b) : StateHandler(b) { importNode = std::make_unique<ImportDecl>(); }
    bool OnMapStart() override { expectingKey = true; return true; }
    bool OnMapEnd() override { builder->popState(); return true; }
    bool OnScalar(const std::string& value, const std::string& tag) override;
    std::unique_ptr<Node> finish() override;
};

// Parses a `config:` node (map value)
class ConfigBuilder : public StateHandler {
    std::unique_ptr<ConfigDecl> configNode;
    std::string currentKey;
    bool expectingKey = false;
    bool insideConfig = false;
public:
    ConfigBuilder(Builder* b) : StateHandler(b) { configNode = std::make_unique<ConfigDecl>(); }
    bool OnMapStart() override;
    bool OnMapEnd() override;
    bool OnScalar(const std::string& value, const std::string& tag) override;
    std::unique_ptr<Node> finish() override;
};

// Parses a `def:` node
class FunctionBuilder : public StateHandler {
    std::unique_ptr<FunctionDecl> func;
    std::string currentKey;
    bool expectingKey = false;
    bool expectingValue = false;
public:
    FunctionBuilder(Builder* b) : StateHandler(b) { func = std::make_unique<FunctionDecl>(); }
    bool OnMapStart() override;
    bool OnMapEnd() override;
    bool OnScalar(const std::string& value, const std::string& tag) override;
    bool OnSequenceStart() override;
    void receiveChildNode(std::unique_ptr<Node> node) override;
    std::unique_ptr<Node> finish() override;
};

// Parses a `if`, `while`, or `ret` block
class ControlFlowBuilder : public StateHandler {
    std::string type;
    std::unique_ptr<Expr> condition;
    std::unique_ptr<BlockStmt> thenBranch;
    std::unique_ptr<BlockStmt> elseBranch; 
    std::unique_ptr<BlockStmt> body;
    
    std::string currentKey;
    bool expectingKey = false;
public:
    ControlFlowBuilder(Builder* b, std::string t) : StateHandler(b), type(t) {}
    bool OnMapStart() override;
    bool OnMapEnd() override;
    bool OnScalar(const std::string& value, const std::string& tag) override;
    bool OnSequenceStart() override;
    void receiveChildNode(std::unique_ptr<Node> node) override;
    std::unique_ptr<Node> finish() override;
};

// Parses an inline expression like `{ add: [1, 2] }`
class ExpressionBuilder : public StateHandler {
    std::string op;
    bool expectingKey = false;
    bool expectingArgs = false;
    std::vector<std::unique_ptr<Expr>> args;
public:
    ExpressionBuilder(Builder* b) : StateHandler(b) {}
    bool OnMapStart() override;
    bool OnMapEnd() override;
    bool OnScalar(const std::string& value, const std::string& tag) override;
    bool OnSequenceStart() override;
    bool OnSequenceEnd() override;
    void receiveChildNode(std::unique_ptr<Node> node) override;
    std::unique_ptr<Node> finish() override;
};

} // namespace AST
} // namespace AIML
