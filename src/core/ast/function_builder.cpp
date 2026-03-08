#include "../../../logger.h"
#include "../ast_builder.h"
#include "function_builder.h"
#include "block_builder.h"

namespace AIML {
namespace AST {

class ArgsBuilder : public StateHandler {
    std::vector<std::pair<std::string, std::string>>* args_out;
    std::string currentArgName;
    std::string pendingType;  // accumulates split type names like "fn<i32"
    int typeDepth = 0;        // unbalanced '<' count in pendingType
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
        if (!pendingType.empty()) {
            // Continuation of a split type name (YAML comma inside angle brackets)
            pendingType += ", " + value;
            for (char c : value) { if (c=='<') ++typeDepth; else if (c=='>') --typeDepth; }
            if (typeDepth <= 0) {
                args_out->push_back({currentArgName, pendingType});
                currentArgName = "";
                pendingType = "";
                typeDepth = 0;
            }
        } else if (currentArgName.empty()) {
            currentArgName = value;
        } else {
            // Check if the type name has unbalanced angle brackets (e.g. "fn<i32")
            int d = 0;
            for (char c : value) { if (c=='<') ++d; else if (c=='>') --d; }
            if (d > 0) {
                pendingType = value;
                typeDepth = d;
            } else {
                args_out->push_back({currentArgName, value});
                currentArgName = "";
            }
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
        else if (currentKey == "version") {
            try { func->version = std::stoull(value); } catch (...) {}
        }

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

} // namespace AST
} // namespace AIML
