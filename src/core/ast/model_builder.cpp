#include "../clearance.h"
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
    if (insideFields && memberAttrIndex == 3) {
        // 4th-position modifier dict: {db_indexed: true, unique: true, ...}.
        // Parsed inline here (flat single-level dict).
        insideModifier = true;
        modifierExpectingKey = true;
        return true;
    }
    if (mapDepth++ > 0) return true;
    expectingKey = true;
    return true;
}

bool ModelBuilder::OnMapEnd() {
    if (insideModifier) {
        insideModifier = false;
        memberAttrIndex++;
        return true;
    }
    if (insideFields) return true;
    builder->popState();
    return true;
}

bool ModelBuilder::OnScalar(const std::string& value, const std::string& tag) {
    if (defaultSeqDepth > 0) { defaultSeqItems = true; return true; }
    if (insideModifier) {
        if (modifierExpectingKey) {
            modifierKey = value;
            modifierExpectingKey = false;
            return true;
        }
        // Apply value to the modifier slot.  Only db_indexed-family keys are
        // recognized today; unknown keys are silently ignored to allow forward
        // compatibility (other modifier families can layer on later).
        if (modifierKey == "alias") {        // spec/15: externer Name
            curAlias = value;
        } else if (modifierKey == "db_indexed") {
            if (value == "true")       { curDbIndexed = true; }
            else if (value == "lower") { curDbIndexed = true; curDbIndexedMode = "lower"; }
            // "false" leaves curDbIndexed = false (default).
        } else if (modifierKey == "unique") {
            curDbIndexedUnique = (value == "true");
        } else if (modifierKey == "level") {   // spec/65: pruefen, nicht durchsetzen
            int parsed = aiml::clearance::kAbsent;
            if (aiml::clearance::parseLevel(value, "model-Feld " + currentField, parsed))
                curLevel = parsed;
        } else if (modifierKey == "skip_empty") {
            curDbIndexedSkipEmpty = (value == "true");
        }
        modifierExpectingKey = true;
        return true;
    }

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
    else if (currentKey == "level") {   // spec/65 F65-19
        int parsed = aiml::clearance::kAbsent;
        if (aiml::clearance::parseLevel(value, "model " + modelNode->name, parsed))
            modelNode->level = parsed;
    }
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
    if (defaultSeqDepth > 0 || (insideFields && memberAttrIndex == 2 && !currentField.empty())) {
        ++defaultSeqDepth;               // a list literal as the field's default
        return true;
    }
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
    if (defaultSeqDepth > 0) {
        if (--defaultSeqDepth == 0) {
            // `[]` is what a list field starts as anyway; a filled default is not supported
            if (defaultSeqItems)
                LOG_ERROR("model " + modelNode->name + ": field '" + currentField +
                          "' has a non-empty list default — fill the list in code (spec/15)");
            defaultSeqItems = false;
            memberAttrIndex++;
        }
        return true;
    }
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
            // AionDB db_indexed modifier captured from optional 4th-position dict.
            m.db_indexed            = curDbIndexed;
            m.db_indexed_mode       = curDbIndexedMode;
            m.db_indexed_unique     = curDbIndexedUnique;
            m.db_indexed_skip_empty = curDbIndexedSkipEmpty;
            m.level                 = curLevel;
            m.alias                 = curAlias;
            modelNode->fields.push_back(std::move(m));
            currentField = ""; currentType = "";
            memberAttrIndex = 0;
            currentAccess = "public";
            curDbIndexed = false; curDbIndexedMode.clear();
            curDbIndexedUnique = false; curDbIndexedSkipEmpty = false;
            curLevel = -1;   // spec/65: nicht ans naechste Feld vererben
            curAlias.clear();  // spec/15: ein Alias gilt genau einem Feld
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
