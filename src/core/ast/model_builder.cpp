#include "../../../logger.h"
#include "../ast_builder.h"
#include "model_builder.h"

namespace AIML {
namespace AST {

bool ModelBuilder::OnMapStart() {
    if (mapDepth++ > 0) return true;
    expectingKey = true;
    return true;
}

bool ModelBuilder::OnMapEnd() {
    if (insideFields) return true;
    builder->popState();
    return true;
}

bool ModelBuilder::OnScalar(const std::string& value, const std::string& tag) {
    if (mapDepth > 1) return true;

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
    else if (currentKey == "primary_key") modelNode->primaryKey = value;
    else if (insideSync) {
        modelNode->syncFields.push_back(value);
        return true;
    }
    else if (insideRpc) {
        modelNode->rpcMethods.push_back(value);
        return true;
    }
    else if (insideFields) {
        if (currentField.empty()) currentField = value;
        else if (currentType.empty()) currentType = value;
    }

    if (!insideFields && !insideSync && !insideRpc) expectingKey = true;
    return true;
}

bool ModelBuilder::OnSequenceStart() {
    if (currentKey == "fields") {
        insideFields = true;
        currentKey = "";
    } else if (currentKey == "sync") {
        insideSync = true;
        currentKey = "";
    } else if (currentKey == "rpc") {
        insideRpc = true;
        currentKey = "";
    } else if (insideFields) {
        currentField = "";
        currentType = "";
    }
    return true;
}

bool ModelBuilder::OnSequenceEnd() {
    if (insideSync) {
        insideSync = false;
        expectingKey = true;
        return true;
    }
    if (insideRpc) {
        insideRpc = false;
        expectingKey = true;
        return true;
    }
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

} // namespace AST
} // namespace AIML
