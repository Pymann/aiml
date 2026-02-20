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

class SkipBuilder : public StateHandler {
    int depth = 0;
public:
    SkipBuilder(Builder* b) : StateHandler(b) {}
    bool OnMapStart() override { depth++; return true; }
    bool OnSequenceStart() override { depth++; return true; }
    bool OnMapEnd() override { 
        if (depth == 0) builder->popState(); 
        else depth--; 
        return true; 
    }
    bool OnSequenceEnd() override { 
        if (depth == 0) builder->popState(); 
        else depth--; 
        return true; 
    }
    bool OnScalar(const std::string&) override { return true; }
};

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
        else if (currentKey == "if" || currentKey == "while" || currentKey == "ret") {
             builder->pushState(std::make_unique<ControlFlowBuilder>(builder, currentKey));
             return true;
        }
        
        // Unhandled top-level (e.g. class, interface) 
        builder->pushState(std::make_unique<SkipBuilder>(builder));
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

bool StatementBuilder::OnMapEnd() {
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
    
    if (type == "return") {
        exprArg = std::make_unique<LiteralExpr>(value, "unknown");
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
    if (type == "return") {
         auto ret = std::make_unique<RetStmt>();
         if (exprArg) ret->value = std::move(exprArg);
         return ret;
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
    return true;
}

bool BlockBuilder::OnMapEnd() {
    return true; 
}

bool BlockBuilder::OnScalar(const std::string& value) {
    if (value == "if" || value == "while" || value == "ret") {
        builder->pushState(std::make_unique<ControlFlowBuilder>(builder, value));
    } else {
        builder->pushState(std::make_unique<StatementBuilder>(builder, value));
    }
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

class ArgsBuilder : public StateHandler {
    std::vector<std::pair<std::string, std::string>>* args_out;
    std::string currentArgName;
    int depth = 1; // Start at 1 because FunctionBuilder consumed the first OnSequenceStart
public:
    ArgsBuilder(Builder* b, std::vector<std::pair<std::string, std::string>>* out) 
        : StateHandler(b), args_out(out) {}
        
    bool OnSequenceStart() override {
        depth++;
        return true;
    }
    
    bool OnSequenceEnd() override {
        depth--;
        if (depth == 0) builder->popState();
        return true;
    }
    
    bool OnScalar(const std::string& value) override {
        if (currentArgName.empty()) {
            currentArgName = value;
        } else {
            args_out->push_back({currentArgName, value});
            currentArgName = "";
        }
        return true;
    }
};

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
        expectingKey = true;
        expectingValue = false;
        return true;
    } else if (currentKey == "args") {
        builder->pushState(std::make_unique<ArgsBuilder>(builder, &func->args));
        expectingKey = true;
        expectingValue = false;
        return true;
    }
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

// --- ControlFlowBuilder Implementation ---

bool ControlFlowBuilder::OnMapStart() {
    expectingKey = true;
    return true; // We are inside e.g. `if: { cond: ..., then: ... }`
}

bool ControlFlowBuilder::OnMapEnd() {
    builder->popState();
    return true;
}

bool ControlFlowBuilder::OnSequenceStart() {
    if (type == "ret") {
        builder->pushState(std::make_unique<ExpressionBuilder>(builder));
        return true;
    }
    
    // An array inside an if/while map is typically a block (e.g., `then: [...]`)
    if (currentKey == "then" || currentKey == "else" || currentKey == "body") {
        builder->pushState(std::make_unique<BlockBuilder>(builder));
        return true;
    }
    
    // Could also be an expression `cond: { eq: [...] }` but the ExpressionBuilder handles its own sequence.
    // If we reach here, maybe `cond` itself is a direct array? Push expression builder.
    builder->pushState(std::make_unique<ExpressionBuilder>(builder));
    return true;
}

bool ControlFlowBuilder::OnScalar(const std::string& value) {
    if (expectingKey) {
        currentKey = value;
        expectingKey = false;
        return true;
    }
    
    // We are expecting a value for `cond:` or `ret:`
    // Example: `ret: 1` or `cond: true`
    if (!expectingKey) {
        if (currentKey == "cond") {
             condition = std::make_unique<LiteralExpr>(value, "unknown");
        } else if (type == "ret") {
             condition = std::make_unique<LiteralExpr>(value, "unknown"); // repurposing condition as return value
        }
        expectingKey = true;
        return true;
    }
    return false;
}

void ControlFlowBuilder::receiveChildNode(std::unique_ptr<Node> node) {
    if (auto e = dynamic_cast<Expr*>(node.release())) {
        if (type == "ret") {
            condition = std::unique_ptr<Expr>(e);
        } else if (currentKey == "cond") {
            condition = std::unique_ptr<Expr>(e);
        }
    } else if (auto b = dynamic_cast<BlockStmt*>(node.release())) {
        if (currentKey == "then") thenBranch = std::unique_ptr<BlockStmt>(b);
        else if (currentKey == "else") elseBranch = std::unique_ptr<BlockStmt>(b);
        else if (currentKey == "body") body = std::unique_ptr<BlockStmt>(b);
    }
    expectingKey = true; // Prepare for next key
}

std::unique_ptr<Node> ControlFlowBuilder::finish() {
    if (type == "if") {
        auto ifStmt = std::make_unique<IfStmt>();
        ifStmt->condition = std::move(condition);
        ifStmt->thenBranch = std::move(thenBranch);
        ifStmt->elseBranch = std::move(elseBranch);
        return ifStmt;
    } else if (type == "while") {
        auto whileStmt = std::make_unique<WhileStmt>();
        whileStmt->condition = std::move(condition);
        whileStmt->body = std::move(body);
        return whileStmt;
    } else if (type == "ret") {
        auto retStmt = std::make_unique<RetStmt>();
        retStmt->value = std::move(condition);
        return retStmt;
    }
    return nullptr;
}

// --- ExpressionBuilder Implementation ---

bool ExpressionBuilder::OnMapStart() {
    if (!op.empty()) {
        // We are already inside an operator like `{ add: [ { mul: [1, 2] }, 3 ] }`
        // We just encountered the inner map `{ mul: ... }`
        builder->pushState(std::make_unique<ExpressionBuilder>(builder));
        return true;
    }
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
        
        // Single-argument operations might just have a scalar value, e.g. `!true`
        // But usually it's `op: [args]`
        return true;
    }
    
    if (expectingArgs) {
        // e.g., `{ add: [a, b] }` inside the array
        // Differentiate literal from variable based on quotes or simple heuristic
        // For prototype, if it's a number/true/false, literal, else var
        if (value == "true" || value == "false" || isdigit(value[0]) || (value.length() > 0 && value[0] == '-')) {
             args.push_back(std::make_unique<LiteralExpr>(value, "unknown"));
        } else {
             args.push_back(std::make_unique<VariableExpr>(value));
        }
        return true;
    }
    
    // Single scalar arg to an operation: e.g. `ret: 5` mapped to expression builder?
    // Let's assume an operation can have a single scalar `not: true`
    if (!op.empty() && !expectingArgs) {
        if (value == "true" || value == "false" || isdigit(value[0]) || (value.length() > 0 && value[0] == '-')) {
             args.push_back(std::make_unique<LiteralExpr>(value, "unknown"));
        } else {
             args.push_back(std::make_unique<VariableExpr>(value));
        }
    }
    
    return true;
}

bool ExpressionBuilder::OnSequenceStart() {
    if (!op.empty() && !expectingArgs) {
        expectingArgs = true;
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
    if (args.size() == 2) {
         return std::make_unique<BinaryExpr>(op, std::move(args[0]), std::move(args[1]));
    }
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
    // Fallback: Just return a literal of the operation or wrap it
    if (args.size() == 1) {
         return std::move(args[0]); // A bit hacky: `not: x` becomes just `x`
    }
    return nullptr;
}

} // namespace AST
} // namespace AIML
