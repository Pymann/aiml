import os
import re

os.makedirs('src/core/ast', exist_ok=True)

try_h = """#pragma once
#include <memory>
#include <string>
#include <vector>
#include <stack>
#include "../ast_nodes.h"
#include "../../logger.h"

namespace AIML {
namespace AST {
class Builder;
class StateHandler;

class TryBuilder : public StateHandler {
    std::unique_ptr<TryStmt> tryStmt;
    std::string currentKey;
    bool expectingKey = true;
    int depth = 0;
    
public:
    TryBuilder(Builder* b) : StateHandler(b), tryStmt(std::make_unique<TryStmt>()) {}

    bool OnMapStart() override {
        depth++;
        expectingKey = true;
        LOG_DEBUG("TryBuilder OnMapStart: depth=" + std::to_string(depth));
        return true;
    }

    bool OnMapEnd() override {
        depth--;
        LOG_DEBUG("TryBuilder OnMapEnd: depth=" + std::to_string(depth));
        if (depth == 0) {
            builder->popState();
        }
        return true;
    }

    bool OnSequenceStart() override {
        depth++;
        LOG_DEBUG("TryBuilder OnSequenceStart: currentKey=" + currentKey + " depth=" + std::to_string(depth));
        if (currentKey == "body") {
            builder->pushState(std::make_unique<BlockBuilder>(builder));
        }
        return true;
    }

    bool OnSequenceEnd() override {
        depth--;
        expectingKey = true;
        LOG_DEBUG("TryBuilder OnSequenceEnd: currentKey=" + currentKey + " depth=" + std::to_string(depth));
        if (depth == 0) {
            builder->popState();
        }
        return true;
    }

    bool OnScalar(const std::string& value, const std::string& tag) override {
        if (expectingKey) {
            currentKey = value;
            expectingKey = false;
            
            if (currentKey == "catch") {
                 expectingKey = true; 
            }
            return true;
        } else {
            if (currentKey == "type") tryStmt->catchType = value;
            else if (currentKey == "var") tryStmt->catchVar = value;
            expectingKey = true;
            return true;
        }
    }

    void receiveChildNode(std::unique_ptr<Node> node) override {
        if (auto b = dynamic_cast<BlockStmt*>(node.get())) {
            b = dynamic_cast<BlockStmt*>(node.release());
            if (currentKey == "body") {
                if (!tryStmt->tryBlock) {
                    LOG_DEBUG("TryBuilder: Received tryBlock with " + std::to_string(b->statements.size()) + " statements");
                    tryStmt->tryBlock = std::unique_ptr<BlockStmt>(b);
                } else {
                    LOG_DEBUG("TryBuilder: Received catchBlock with " + std::to_string(b->statements.size()) + " statements");
                    tryStmt->catchBlock = std::unique_ptr<BlockStmt>(b);
                }
            }
        }
        expectingKey = true;
    }

    std::unique_ptr<Node> finish() override {
        return std::move(tryStmt);
    }
};

} // namespace AST
} // namespace AIML
"""
with open('src/core/ast/try_builder.h', 'w') as f: f.write(try_h)

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

def to_snake(name):
    s1 = re.sub('(.)([A-Z][a-z]+)', r'\1_\2', name)
    return re.sub('([a-z0-9])([A-Z])', r'\1_\2', s1).lower()

classes = [
    "RootState", "StatementBuilder", "BlockBuilder", "ClassBuilder", 
    "ModelBuilder", "InterfaceBuilder", "EnumBuilder", "NamespaceBuilder", 
    "ImportBuilder", "ConfigBuilder", "FunctionBuilder", "ControlFlowBuilder", 
    "ExpressionBuilder"
]

for cls in classes:
    m = re.search(r'(//.*?\n)?class ' + cls + r' : public StateHandler \{(.*?)\};\n', old_h, re.DOTALL)
    if m:
        h_code = m.group(0)
        snake = to_snake(cls)
        # Add basic imports logic to ensure correct behavior.
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

print("Done restoring valid header definitions.")
