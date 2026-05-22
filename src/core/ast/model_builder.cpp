#include "../../../logger.h"
#include "../ast_builder.h"
#include "model_builder.h"
#include "expression_builder.h"

namespace AIML {
namespace AST {

bool ModelBuilder::OnMapStart() {
    if (insideFields && memberAttrIndex == 2) {
        auto exprBuilder = std::make_unique<ExpressionBuilder>(builder);
        exprBuilder->OnMapStart();
        builder->pushState(std::move(exprBuilder));
        return true;
    }
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
    else if (currentKey == "inherits") modelNode->parentName = value;
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
        if (memberAttrIndex == 0) {
            currentField = value;
            memberAttrIndex++;
            return true;
        }
        if (memberAttrIndex == 1) {
            currentType = value;
            memberAttrIndex++;
            return true;
        }
        if (memberAttrIndex == 2) {
            if (tag == "!") {
                currentInitValue = std::make_unique<LiteralExpr>(value, "string");
            } else if (value == "true" || value == "false" || (!value.empty() && (isdigit(value[0]) || value[0] == '-'))) {
                currentInitValue = std::make_unique<LiteralExpr>(value, "unknown");
            } else {
                currentInitValue = std::make_unique<VariableExpr>(value);
            }
            memberAttrIndex++;
            return true;
        }
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
        memberAttrIndex = 0;
        currentInitValue = nullptr;
        currentAccess = "public";
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
            m.initialValue = std::move(currentInitValue);
            m.access = currentAccess;
            modelNode->fields.push_back(std::move(m));
            currentField = ""; currentType = "";
            memberAttrIndex = 0;
            currentAccess = "public";
            return true;
        } else {
            insideFields = false;
            expectingKey = true;
        }
    }
    return true;
}

void ModelBuilder::receiveChildNode(std::unique_ptr<Node> node) {
    if (insideFields && memberAttrIndex == 2) {
        if (auto* e = dynamic_cast<Expr*>(node.get())) {
            currentInitValue = std::unique_ptr<Expr>(e);
            node.release();
            memberAttrIndex++;
        }
    }
}
std::unique_ptr<Node> ModelBuilder::finish() { return std::move(modelNode); }

} // namespace AST
} // namespace AIML
