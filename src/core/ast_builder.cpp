#include "ast_builder.h"
#include <iostream>
#include <yaml-cpp/yaml.h>

namespace AIML {
namespace AST {

static std::unique_ptr<Expr> parseInterpolatedString(const std::string& input);

static std::unique_ptr<Expr> nodeToExpr(const YAML::Node& node) {
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
        std::string op = node.begin()->first.as<std::string>();
        auto argsNode = node.begin()->second;
        
        if (op == "call" && argsNode.IsSequence() && argsNode.size() > 0) {
            std::string funcName;
            if (argsNode[0].IsScalar()) funcName = argsNode[0].as<std::string>();
            else if (argsNode[0].IsMap() && argsNode[0].begin()->first.as<std::string>() == "call") {
                // Should not happen as function name 
            }
            auto callExpr = std::make_unique<CallExpr>(funcName);
            for(size_t i=1; i<argsNode.size(); ++i) {
                callExpr->args.push_back(nodeToExpr(argsNode[i]));
            }
            return callExpr;
        }
        
        // binary Expr
        if (argsNode.IsSequence() && argsNode.size() == 2) {
             return std::make_unique<BinaryExpr>(op, nodeToExpr(argsNode[0]), nodeToExpr(argsNode[1]));
        }
    }
    return nullptr;
}

static std::unique_ptr<Expr> parseInterpolatedString(const std::string& input) {
    if (input.find("${") == std::string::npos) {
        return std::make_unique<LiteralExpr>(input, "string");
    }
    
    auto interp = std::make_unique<StringInterpolationExpr>();
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
        
        size_t end = input.find("}", start);
        if (end == std::string::npos) break; // Malformed
        
        std::string exprStr = input.substr(start + 2, end - start - 2);
        try {
            YAML::Node node = YAML::Load(exprStr);
            auto astExpr = nodeToExpr(node);
            if (astExpr) {
                interp->parts.push_back(std::move(astExpr));
            } else {
                interp->parts.push_back(std::make_unique<LiteralExpr>("${" + exprStr + "}", "string"));
            }
        } catch (...) {
            interp->parts.push_back(std::make_unique<LiteralExpr>("${" + exprStr + "}", "string"));
        }
        
        pos = end + 1;
    }
    
    if (interp->parts.size() == 1) {
        return std::move(interp->parts[0]);
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
void Builder::OnScalar(const YAML::Mark&, const std::string& tag, YAML::anchor_t, const std::string& value) {
    stateStack.top()->OnScalar(value, tag);
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
    bool OnScalar(const std::string&, const std::string&) override { return true; }
};

bool RootState::OnScalar(const std::string& value, const std::string& tag) {
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
        else if (currentKey == "class") {
             builder->pushState(std::make_unique<ClassBuilder>(builder));
             return true;
        }
        else if (currentKey == "model") {
             builder->pushState(std::make_unique<ModelBuilder>(builder));
             return true;
        }
        else if (currentKey == "interface") {
             builder->pushState(std::make_unique<InterfaceBuilder>(builder));
             return true;
        }
        else if (currentKey == "enum") {
             builder->pushState(std::make_unique<EnumBuilder>(builder));
             return true;
        }
        else if (currentKey == "namespace") {
             builder->pushState(std::make_unique<NamespaceBuilder>(builder));
             return true;
        }
        else if (currentKey == "import") {
             builder->pushState(std::make_unique<ImportBuilder>(builder));
             return true;
        }
        else if (currentKey == "config") {
             builder->pushState(std::make_unique<ConfigBuilder>(builder));
             return true;
        }
        
        // Unhandled top-level 
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

bool StatementBuilder::OnScalar(const std::string& value, const std::string& tag) {
    if (expectingValue) {
        // We received the RHS of a `set: [x, value]`
        if (tag == "!") {
            if (value.find("${") != std::string::npos) exprArg = parseInterpolatedString(value);
            else exprArg = std::make_unique<LiteralExpr>(value, "string");
        } else if (value == "true" || value == "false" || isdigit(value[0]) || (value.length() > 0 && value[0] == '-')) {
            exprArg = std::make_unique<LiteralExpr>(value, "unknown");
        } else {
            exprArg = std::make_unique<VariableExpr>(value);
        }
        expectingValue = false;
        return true;
    }
    
    // Collecting arguments inside the sequence `[x, i32, 5]`
    args.push_back({value, tag});
    
    if (type == "set" && args.size() == 1) {
        // After reading `x`, the next thing might be an expression map instead of a scalar
        expectingValue = true; 
    }
    
    if (type == "return") {
        if (tag == "!") {
            if (value.find("${") != std::string::npos) exprArg = parseInterpolatedString(value);
            else exprArg = std::make_unique<LiteralExpr>(value, "string");
        } else if (value == "true" || value == "false" || isdigit(value[0]) || (value.length() > 0 && value[0] == '-')) {
            exprArg = std::make_unique<LiteralExpr>(value, "unknown");
        } else {
            exprArg = std::make_unique<VariableExpr>(value);
        }
    }
    
    return true;
}

bool StatementBuilder::OnMapStart() {
    // A statement received a map, likely an expression like `set: [x, { add: [1, 2] }]`
    expectingValue = false;
    auto exprBuilder = std::make_unique<ExpressionBuilder>(builder);
    exprBuilder->OnMapStart();
    builder->pushState(std::move(exprBuilder));
    return true;
}

void StatementBuilder::receiveChildNode(std::unique_ptr<Node> node) {
    if (auto e = dynamic_cast<Expr*>(node.release())) {
        exprArg = std::unique_ptr<Expr>(e);
    }
}

std::unique_ptr<Node> StatementBuilder::finish() {
    if (type == "decl" && args.size() >= 2) {
         auto decl = std::make_unique<DeclStmt>(args[0].first, args[1].first);
         if (exprArg) decl->initialValue = std::move(exprArg);
         else if (args.size() >= 3) {
             const std::string& val = args[2].first;
             const std::string& tag = args[2].second;
             if (tag == "!") {
                 if (val.find("${") != std::string::npos) decl->initialValue = parseInterpolatedString(val);
                 else decl->initialValue = std::make_unique<LiteralExpr>(val, "string");
             } else if (val == "true" || val == "false" || isdigit(val[0]) || (val.length() > 0 && val[0] == '-')) {
                 decl->initialValue = std::make_unique<LiteralExpr>(val, "unknown");
             } else {
                 decl->initialValue = std::make_unique<VariableExpr>(val);
             }
         }
         return decl;
    }
    if (type == "set" && args.size() >= 1) {
         if (!exprArg && args.size() >= 2) {
             const std::string& val = args[1].first;
             const std::string& tag = args[1].second;
             if (tag == "!") {
                 if (val.find("${") != std::string::npos) exprArg = parseInterpolatedString(val);
                 else exprArg = std::make_unique<LiteralExpr>(val, "string");
             } else if (val == "true" || val == "false" || isdigit(val[0]) || (val.length() > 0 && val[0] == '-')) {
                 exprArg = std::make_unique<LiteralExpr>(val, "unknown");
             } else {
                 exprArg = std::make_unique<VariableExpr>(val);
             }
         }
         auto set = std::make_unique<SetStmt>(args[0].first, std::move(exprArg));
         return set;
    }
    if (type == "call" && args.size() >= 1) {
         auto call = std::make_unique<CallExpr>(args[0].first);
         for (size_t i = 1; i < args.size(); ++i) {
             const std::string& val = args[i].first;
             const std::string& tag = args[i].second;
             if (tag == "!") {
                  if (val.find("${") != std::string::npos) call->args.push_back(parseInterpolatedString(val));
                  else call->args.push_back(std::make_unique<LiteralExpr>(val, "string"));
             } else if (val == "true" || val == "false" || isdigit(val[0]) || (val.length() > 0 && val[0] == '-')) {
                  call->args.push_back(std::make_unique<LiteralExpr>(val, "unknown"));
             } else {
                  call->args.push_back(std::make_unique<VariableExpr>(val));
             }
         }
         if (exprArg) call->args.push_back(std::move(exprArg));
         
         return std::make_unique<ExprStmt>(std::move(call));
    }
    if (type == "print" && args.size() >= 1) {
         auto call = std::make_unique<CallExpr>("print");
         if (args[0].second == "!") {
             if (args[0].first.find("${") != std::string::npos) call->args.push_back(parseInterpolatedString(args[0].first));
             else call->args.push_back(std::make_unique<LiteralExpr>(args[0].first, "string"));
         } else {
             call->args.push_back(std::make_unique<VariableExpr>(args[0].first));
         }
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

bool BlockBuilder::OnScalar(const std::string& value, const std::string& tag) {
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
    
    bool OnScalar(const std::string& value, const std::string& tag) override {
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

bool FunctionBuilder::OnScalar(const std::string& value, const std::string& tag) {
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
    auto exprBuilder = std::make_unique<ExpressionBuilder>(builder);
    if (!expectingKey && currentKey == "cond") {
        exprBuilder->OnMapStart();
    }
    builder->pushState(std::move(exprBuilder));
    return true;
}

bool ControlFlowBuilder::OnScalar(const std::string& value, const std::string& tag) {
    if (expectingKey) {
        currentKey = value;
        expectingKey = false;
        return true;
    }
    
    // We are expecting a value for `cond:` or `ret:`
    // Example: `ret: 1` or `cond: true`
    if (!expectingKey) {
        if (currentKey == "cond" || type == "ret") {
             if (tag == "!") {
                 if (value.find("${") != std::string::npos) condition = parseInterpolatedString(value);
                 else condition = std::make_unique<LiteralExpr>(value, "string");
             } else if (value == "true" || value == "false" || isdigit(value[0]) || (value.length() > 0 && value[0] == '-')) {
                 condition = std::make_unique<LiteralExpr>(value, "unknown");
             } else {
                 condition = std::make_unique<VariableExpr>(value);
             }
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

bool ExpressionBuilder::OnScalar(const std::string& value, const std::string& tag) {
    if (expectingKey) {
        op = value;
        expectingKey = false;
        
        // Single-argument operations might just have a scalar value, e.g. `!true`
        // But usually it's `op: [args]`
        return true;
    }
    
    if (expectingArgs || (!op.empty() && !expectingArgs)) {
        if (tag == "!") {
            if (value.find("${") != std::string::npos) args.push_back(parseInterpolatedString(value));
            else args.push_back(std::make_unique<LiteralExpr>(value, "string"));
        } else if (value == "true" || value == "false" || isdigit(value[0]) || (value.length() > 0 && value[0] == '-')) {
             args.push_back(std::make_unique<LiteralExpr>(value, "unknown"));
        } else {
             args.push_back(std::make_unique<VariableExpr>(value));
        }
        return true;
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
    if (op == "new" && !args.empty()) {
         auto newName = dynamic_cast<VariableExpr*>(args[0].get());
         auto newLiteral = dynamic_cast<LiteralExpr*>(args[0].get());
         std::string name = "unknown_class";
         if (newName) name = newName->name;
         else if (newLiteral) name = newLiteral->value;
         
         return std::make_unique<NewExpr>(name);
    }
    // Fallback: Just return a literal of the operation or wrap it
    if (args.size() == 1) {
         return std::move(args[0]); // A bit hacky: `not: x` becomes just `x`
    }
    return nullptr;
}

// --- ClassBuilder Implementation ---

bool ClassBuilder::OnMapStart() {
    expectingKey = true;
    // If inside methods sequence and we start a map, it must be a method definition (e.g., `- def:`)
    if (insideMethods && !expectingKey) {
         // The YAML parser will send OnScalar("def") next.
         // We can push a SkipBuilder for now, or let FunctionBuilder handle it.
         // Actually, if it's a list of maps, OnMapStart triggers here.
    }
    return true;
}

bool ClassBuilder::OnMapEnd() {
    if (insideMethods) return true; 
    if (insideMembers) return true; // Ignore nested maps inside members like modifiers
    builder->popState();
    return true;
}

bool ClassBuilder::OnScalar(const std::string& value, const std::string& tag) {
    if (expectingKey) {
        currentKey = value;
        expectingKey = false;
        
        if (insideMethods && currentKey == "def") {
             builder->pushState(std::make_unique<FunctionBuilder>(builder));
             return true;
        }
        return true;
    }
    
    if (currentKey == "name") classNode->name = value;
    else if (currentKey == "extends") classNode->parentName = value;
    else if (currentKey == "version") {
        if (!value.empty()) {
            try { classNode->version = std::stoull(value); } catch (...) {}
        }
    }
    else if (insideMembers) {
        if (currentField.empty()) currentField = value;
        else if (currentType.empty()) currentType = value;
        // third value would be default, ignoring for now in AST
    }
    
    if (!insideMembers && !insideMethods) expectingKey = true;
    return true;
}

bool ClassBuilder::OnSequenceStart() {
    if (currentKey == "members") insideMembers = true;
    else if (currentKey == "methods") insideMethods = true;
    else if (insideMembers) {
        currentField = "";
        currentType = "";
    }
    return true;
}

bool ClassBuilder::OnSequenceEnd() {
    if (insideMembers) {
        if (!currentField.empty() && !currentType.empty()) {
            StructMember m; m.name = currentField; m.type = currentType;
            classNode->members.push_back(std::move(m));
            currentField = ""; currentType = "";
            return true;
        } else {
            insideMembers = false;
            expectingKey = true;
        }
    } else if (insideMethods) {
        insideMethods = false;
        expectingKey = true;
    }
    return true;
}

void ClassBuilder::receiveChildNode(std::unique_ptr<Node> node) {
    if (auto f = dynamic_cast<FunctionDecl*>(node.get())) {
        classNode->methods.push_back(std::unique_ptr<FunctionDecl>(static_cast<FunctionDecl*>(node.release())));
        expectingKey = true; // wait for next map or end of sequence
    }
}

std::unique_ptr<Node> ClassBuilder::finish() {
    return std::move(classNode);
}

// --- ModelBuilder Implementation ---

bool ModelBuilder::OnMapStart() {
    expectingKey = true;
    return true;
}

bool ModelBuilder::OnMapEnd() {
    if (insideFields) return true;
    builder->popState();
    return true;
}

bool ModelBuilder::OnScalar(const std::string& value, const std::string& tag) {
    if (expectingKey) {
        currentKey = value;
        expectingKey = false;
        return true;
    }
    
    if (currentKey == "name") modelNode->name = value;
    else if (currentKey == "version") {
        if (!value.empty()) {
            try { modelNode->version = std::stoull(value); } catch (...) {}
        }
    }
    else if (insideFields) {
        if (currentField.empty()) currentField = value;
        else if (currentType.empty()) currentType = value;
    }
    
    if (!insideFields) expectingKey = true;
    return true;
}

bool ModelBuilder::OnSequenceStart() {
    if (currentKey == "fields") insideFields = true;
    else if (insideFields) {
        currentField = "";
        currentType = "";
    }
    return true;
}

bool ModelBuilder::OnSequenceEnd() {
    if (insideFields) {
        if (!currentField.empty() && !currentType.empty()) {
            StructMember m; m.name = currentField; m.type = currentType;
            modelNode->fields.push_back(std::move(m));
            currentField = ""; currentType = "";
            return true;
        } else {
            insideFields = false;
            expectingKey = true;
        }
    }
    return true;
}

void ModelBuilder::receiveChildNode(std::unique_ptr<Node> node) { }
std::unique_ptr<Node> ModelBuilder::finish() { return std::move(modelNode); }


// --- InterfaceBuilder Implementation ---

bool InterfaceBuilder::OnMapStart() {
    expectingKey = true;
    return true;
}
bool InterfaceBuilder::OnMapEnd() {
    builder->popState();
    return true;
}
bool InterfaceBuilder::OnScalar(const std::string& value, const std::string& tag) {
    if (expectingKey) {
        currentKey = value;
        expectingKey = false;
        return true;
    }
    if (currentKey == "name") interfaceNode->name = value;
    else if (insideMethodDef) {
        if (currentMethod.name.empty()) currentMethod.name = value;
        else if (currentMethod.returnType.empty()) currentMethod.returnType = value;
        // arguments would be in next sequence
    }
    if (!insideMethods) expectingKey = true;
    return true;
}
bool InterfaceBuilder::OnSequenceStart() {
    if (currentKey == "methods") insideMethods = true;
    else if (insideMethods) { insideMethodDef = true; currentMethod.name = ""; currentMethod.returnType = ""; }
    return true;
}
bool InterfaceBuilder::OnSequenceEnd() {
    if (insideMethodDef) {
        if (!currentMethod.name.empty()) {
            interfaceNode->methods.push_back(currentMethod);
        }
        insideMethodDef = false;
    } else if (insideMethods) {
        insideMethods = false;
        expectingKey = true;
    }
    return true;
}
std::unique_ptr<Node> InterfaceBuilder::finish() { return std::move(interfaceNode); }

// --- EnumBuilder Implementation ---

bool EnumBuilder::OnMapStart() {
    expectingKey = true;
    return true;
}
bool EnumBuilder::OnMapEnd() {
    // If inside values map: `Green: 1`
    if (insideValues) {
        if (!activeValueName.empty() && overrideValue != -1) {
             enumNode->values.push_back({activeValueName, overrideValue});
        }
        activeValueName = ""; overrideValue = -1;
        return true; 
    }
    builder->popState();
    return true;
}
bool EnumBuilder::OnScalar(const std::string& value, const std::string& tag) {
    if (expectingKey) {
        currentKey = value;
        expectingKey = false;
        if (insideValues) { activeValueName = value; expectingKey = false; }
        return true;
    }
    if (currentKey == "name") enumNode->name = value;
    else if (insideValues) {
        if (!activeValueName.empty()) { overrideValue = std::stoi(value); }
        else { enumNode->values.push_back({value, -1}); }
    }
    if (!insideValues) expectingKey = true;
    return true;
}
bool EnumBuilder::OnSequenceStart() {
    if (currentKey == "values") insideValues = true;
    return true;
}
bool EnumBuilder::OnSequenceEnd() {
    if (insideValues) { insideValues = false; expectingKey = true; }
    return true;
}
std::unique_ptr<Node> EnumBuilder::finish() { 
    // Fix implicit values
    int current = 0;
    for (auto& pair : enumNode->values) {
         if (pair.second != -1) current = pair.second;
         else pair.second = current;
         current++;
    }
    return std::move(enumNode); 
}

// --- Modular Builders Implementation ---

bool NamespaceBuilder::OnScalar(const std::string& value, const std::string& tag) {
    if (expectingKey) {
        currentKey = value;
        expectingKey = false;
        return true;
    }
    if (currentKey == "namespace") nsNode->name = value;
    return true;
}
std::unique_ptr<Node> NamespaceBuilder::finish() { return std::move(nsNode); }

bool ImportBuilder::OnScalar(const std::string& value, const std::string& tag) {
    if (expectingKey) {
        currentKey = value;
        expectingKey = false;
        return true;
    }
    if (currentKey == "import") importNode->path = value;
    return true;
}
std::unique_ptr<Node> ImportBuilder::finish() { return std::move(importNode); }

bool ConfigBuilder::OnMapStart() {
    expectingKey = true;
    return true;
}
bool ConfigBuilder::OnMapEnd() {
    if (insideConfig) {
        insideConfig = false;
        return true;
    }
    builder->popState();
    return true;
}
bool ConfigBuilder::OnScalar(const std::string& value, const std::string& tag) {
    if (expectingKey) {
        currentKey = value;
        expectingKey = false;
        if (currentKey == "config" && !insideConfig) {
             insideConfig = true; // The value is a map, so next event is MapStart
             expectingKey = true;
        }
        return true;
    }
    if (insideConfig) {
        if (currentKey == "entrypoint") configNode->entryPoint = value;
        else if (currentKey == "stack_size") configNode->stackSize = std::stoull(value);
        else if (currentKey == "heap_size") configNode->heapSize = std::stoull(value);
        else if (currentKey == "target") configNode->target = value;
        else if (currentKey == "version") configNode->version = value;
        expectingKey = true;
    }
    return true;
}
std::unique_ptr<Node> ConfigBuilder::finish() { return std::move(configNode); }

} // namespace AST
} // namespace AIML
