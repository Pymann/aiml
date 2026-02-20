#include "ast_builder.h"
#include <iostream>

namespace AIML {
namespace AST {

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
    stateStack.push(std::move(state));
}

void Builder::popState() {
    if (stateStack.size() > 1) {
        auto finishedState = std::move(stateStack.top());
        stateStack.pop();
        
        auto finishedNode = finishedState->finish();
        if (finishedNode) {
            stateStack.top()->receiveChildNode(std::move(finishedNode));
        }
    }
}

// Event Router
void Builder::OnScalar(const YAML::Mark&, const std::string&, YAML::anchor_t, const std::string& value) {
    stateStack.top()->OnScalar(value);
}

void Builder::OnSequenceStart(const YAML::Mark&, const std::string&, YAML::anchor_t, YAML::EmitterStyle::value) {
    stateStack.top()->OnSequenceStart();
}

void Builder::OnSequenceEnd() {
    stateStack.top()->OnSequenceEnd();
}

void Builder::OnMapStart(const YAML::Mark&, const std::string&, YAML::anchor_t, YAML::EmitterStyle::value) {
    stateStack.top()->OnMapStart();
}

void Builder::OnMapEnd() {
    stateStack.top()->OnMapEnd();
}

// --- RootState Implementation ---

bool RootState::OnMapStart() {
    expectingKey = true;
    return true;
}

bool RootState::OnMapEnd() {
    return true; 
}

bool RootState::OnScalar(const std::string& value) {
    if (expectingKey) {
        currentKey = value;
        expectingKey = false;
        
        // Push a new handler based on the top-level key
        if (currentKey == "def") {
             builder->pushState(std::make_unique<FunctionBuilder>(builder));
             return true;
        }
        else if (currentKey == "decl" || currentKey == "set" || currentKey == "call" || currentKey == "print") {
             builder->pushState(std::make_unique<StatementBuilder>(builder, currentKey));
             return true;
        }
        
        // Unhandled top-level (e.g. class, interface) for now - just push a dummy map consumer so we don't crash
        // or just return true and ignore its contents. Let's ignore for prototype.
        return true;
    }
    return false;
}

void RootState::receiveChildNode(std::unique_ptr<Node> node) {
    if (node) {
        builder->addDeclaration(std::move(node));
    }
}

// --- StatementBuilder Implementation ---

bool StatementBuilder::OnSequenceStart() {
    return true; // e.g. `decl: [x, i32]`
}

bool StatementBuilder::OnSequenceEnd() {
    builder->popState();
    return true;
}

bool StatementBuilder::OnScalar(const std::string& value) {
    if (expectingValue) {
        // We received the RHS of a `set: [x, value]`
        exprArg = std::make_unique<LiteralExpr>(value, "unknown");
        expectingValue = false;
        return true;
    }
    
    // Collecting arguments inside the sequence `[x, i32, 5]`
    args.push_back(value);
    
    if (type == "set" && args.size() == 1) {
        // After reading `x`, the next thing might be an expression map instead of a scalar
        expectingValue = true; 
    }
    
    return true;
}

bool StatementBuilder::OnMapStart() {
    // A statement received a map, likely an expression like `set: [x, { add: [1, 2] }]`
    expectingValue = false;
    builder->pushState(std::make_unique<ExpressionBuilder>(builder));
    return true;
}

void StatementBuilder::receiveChildNode(std::unique_ptr<Node> node) {
    if (auto e = dynamic_cast<Expr*>(node.release())) {
        exprArg = std::unique_ptr<Expr>(e);
    }
}

std::unique_ptr<Node> StatementBuilder::finish() {
    if (type == "decl" && args.size() >= 2) {
         auto decl = std::make_unique<DeclStmt>(args[0], args[1]);
         if (exprArg) decl->initialValue = std::move(exprArg);
         else if (args.size() >= 3) decl->initialValue = std::make_unique<LiteralExpr>(args[2], "unknown");
         return decl;
    }
    if (type == "set" && args.size() >= 1) {
         auto set = std::make_unique<SetStmt>(args[0], std::move(exprArg));
         return set;
    }
    if (type == "call" && args.size() >= 1) {
         auto call = std::make_unique<CallExpr>(args[0]);
         // TODO arguments
         return std::make_unique<ExprStmt>(std::move(call));
    }
    if (type == "print" && args.size() >= 1) {
         auto call = std::make_unique<CallExpr>("print");
         call->args.push_back(std::make_unique<LiteralExpr>(args[0], "string"));
         return std::make_unique<ExprStmt>(std::move(call));
    }
    return nullptr;
}

// --- BlockBuilder Implementation ---

bool BlockBuilder::OnSequenceStart() {
    return true; // The block itself is a sequence
}

bool BlockBuilder::OnSequenceEnd() {
    builder->popState();
    return true;
}

bool BlockBuilder::OnMapStart() {
    // Each item in the block is a statement map (e.g., `{ set: [x, 5] }`)
    // Because we are streaming, we need a generic map consumer that figures out what statement it is
    // We'll peek at the key when OnScalar fires inside RootState-like logic.
    // For now, let's treat the block like RootState where it creates specific statement builders.
    
    // Actually, when OnMapStart fires, we push a "BlockItemBuilder" or just let BlockBuilder handle it.
    // Let's implement a BlockItemBuilder dynamically.
    class BlockItemBuilder : public StateHandler {
        std::string key;
        bool expectKey = true;
    public:
        BlockItemBuilder(Builder* b) : StateHandler(b) {}
        bool OnScalar(const std::string& v) override {
            if (expectKey) {
                key = v;
                expectKey = false;
                builder->pushState(std::make_unique<StatementBuilder>(builder, key));
                return true;
            }
            return false;
        }
        bool OnMapEnd() override { builder->popState(); return true; }
        void receiveChildNode(std::unique_ptr<Node> n) override {
            // Forward the built statement up to the BlockBuilder
            builder->popState(); // pop ourself
            // Note: This is tricky, receiveChildNode is called ON the parent.
            // When StatementBuilder finishes, it calls receiveChildNode on BlockItemBuilder.
            // We need to pass it to BlockBuilder. 
        }
    };
    
    // Instead of the inline class trick which makes receiveChildNode messy with the stack, 
    // let's do it cleanly:
    
    return true;
}

// Rewriting BlockBuilder to be robust
bool BlockBuilder::OnScalar(const std::string& value) {
    // If we receive a scalar while expecting a Map Key in the block sequence
    // It means it's a statement type like "set", "decl", etc.
    builder->pushState(std::make_unique<StatementBuilder>(builder, value));
    return true;
}

void BlockBuilder::receiveChildNode(std::unique_ptr<Node> node) {
    if (auto stmt = dynamic_cast<Stmt*>(node.release())) {
        block->statements.push_back(std::unique_ptr<Stmt>(stmt));
    }
}

std::unique_ptr<Node> BlockBuilder::finish() {
    return std::move(block);
}

// --- FunctionBuilder Implementation ---

bool FunctionBuilder::OnMapStart() {
    // def is a map: { name: foo, return: i32, args: [...], body: [...] }
    expectingKey = true;
    return true;
}

bool FunctionBuilder::OnMapEnd() {
    builder->popState();
    return true;
}

bool FunctionBuilder::OnSequenceStart() {
    if (currentKey == "body") {
        builder->pushState(std::make_unique<BlockBuilder>(builder));
        return true;
    }
    // TODO: Args sequence parsing
    return true;
}

bool FunctionBuilder::OnScalar(const std::string& value) {
    if (expectingKey) {
        currentKey = value;
        expectingKey = false;
        expectingValue = true;
        return true;
    }
    
    if (expectingValue) {
        if (currentKey == "name") func->name = value;
        else if (currentKey == "return") func->returnType = value;
        
        expectingKey = true;
        expectingValue = false;
        return true;
    }
    return false;
}

void FunctionBuilder::receiveChildNode(std::unique_ptr<Node> node) {
    if (currentKey == "body") {
        if (auto b = dynamic_cast<BlockStmt*>(node.release())) {
            func->body = std::unique_ptr<BlockStmt>(b);
        }
        expectingKey = true;
    }
}

std::unique_ptr<Node> FunctionBuilder::finish() {
    return std::move(func);
}

// --- ExpressionBuilder Implementation ---

bool ExpressionBuilder::OnMapStart() {
    expectingKey = true;
    return true;
}

bool ExpressionBuilder::OnMapEnd() {
    builder->popState();
    return true;
}

bool ExpressionBuilder::OnScalar(const std::string& value) {
    if (expectingKey) {
        op = value;
        expectingKey = false;
        expectingArgs = true;
        return true;
    }
    
    if (expectingArgs) {
        // e.g., { add: [a, b] } inside the array
        args.push_back(std::make_unique<VariableExpr>(value)); // simplification
        return true;
    }
    
    return false;
}

bool ExpressionBuilder::OnSequenceStart() {
    return true;
}

bool ExpressionBuilder::OnSequenceEnd() {
    return true;
}

void ExpressionBuilder::receiveChildNode(std::unique_ptr<Node> node) {
    if (auto e = dynamic_cast<Expr*>(node.release())) {
        args.push_back(std::unique_ptr<Expr>(e));
    }
}

std::unique_ptr<Node> ExpressionBuilder::finish() {
    if (args.size() == 2) {
         return std::make_unique<BinaryExpr>(op, std::move(args[0]), std::move(args[1]));
    }
    if (op == "call" && !args.empty()) {
         auto call = std::make_unique<CallExpr>("complex_call");
         return call;
    }
    return nullptr;
}

} // namespace AST
} // namespace AIML
