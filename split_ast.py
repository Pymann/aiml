import os
import re

os.makedirs('src/core/ast', exist_ok=True)

with open('src/core/ast_builder.h', 'r') as f:
    h_txt = f.read()

classes = [
    "RootState", "StatementBuilder", "BlockBuilder", "ClassBuilder", 
    "ModelBuilder", "InterfaceBuilder", "EnumBuilder", "NamespaceBuilder", 
    "ImportBuilder", "ConfigBuilder", "FunctionBuilder", "ControlFlowBuilder", 
    "ExpressionBuilder"
]

def to_snake(name):
    # Fix the snake case regex here that broke NamespaceBuilder
    s1 = "".join(['_' + c.lower() if c.isupper() else c for c in name]).strip('_')
    return s1

# We use the old_h variable from recover.py that has all classes without namespaces
old_h = """class RootState : public StateHandler {
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
    bool insideModels = false;
    bool insideImplements = false;
    int mapDepth = 0; // Start at 0
    int sequenceDepth = 0;
    int memberAttrIndex = 0; // Tracks which column [name, type, default, modifiers]
    
    // State for parsing member sequence entry
    std::string currentField;
    std::string currentType;
    std::unique_ptr<Expr> currentInitValue;
    uint64_t currentVersion = 1;
    std::string currentAccess = "public";
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
    int mapDepth = 0; // Start at 0
    
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
"""

for cls in classes:
    start_str = f"class {cls} : public StateHandler {{"
    start_idx = old_h.find(start_str)
    if start_idx == -1:
        continue
        
    # Track brackets to find the end of the class
    search_idx = start_idx + len(start_str) - 1 # starts at '{'
    bracket_count = 0
    end_idx = -1
    
    for i in range(search_idx, len(old_h)):
        if old_h[i] == '{':
            bracket_count += 1
        elif old_h[i] == '}':
            bracket_count -= 1
            if bracket_count == 0:
                end_idx = old_h.find(';', i) + 1
                break
                
    if end_idx == -1:
        print(f"Failed to find end for {cls}")
        continue
        
    comment_start = old_h.rfind("//", 0, start_idx)
    nl_before_comment = old_h.find("\n", comment_start, start_idx)
    if comment_start != -1 and nl_before_comment == -1: # It's immediately before
        start_idx = comment_start

    h_code = old_h[start_idx:end_idx].strip()
    
    snake = to_snake(cls)
    with open(f'src/core/ast/{snake}.h', 'w') as f:
        f.write(f"""#pragma once
#include <memory>
#include <string>
#include <vector>
#include <stack>
#include "../ast_nodes.h"

namespace AIML {{
namespace AST {{

class Builder;
class StateHandler;

{h_code}

}} // namespace AST
}} // namespace AIML
""")

print("Successfully generated independent headers.")
