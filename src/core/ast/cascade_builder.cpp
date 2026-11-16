#include "../clearance.h"
#include "cascade_builder.h"
#include "../ast_builder.h"
#include "../../../logger.h"
#include <stdexcept>

namespace AIML {
namespace AST {

CascadeBuilder::CascadeBuilder(Builder* b)
    : StateHandler(b), node_(std::make_unique<CascadeDecl>()) {}

bool CascadeBuilder::OnMapStart() {
    ++depth;
    switch (where) {
        case Where::TopLevel:
            // depth becomes 1 — cascade body open
            expectingKey = true;
            break;
        case Where::NodesSeq:
            // Entering a single node item `{ name: [...] }` or `{ name: { id: ... } }`
            where = Where::NodeItemMap;
            currentNode = CascadeNode{};
            expectingKey = true;
            break;
        case Where::NodeItemMap:
            // Entering explicit-form body: `name: { id: N, fields: [...] }`
            where = Where::NodeExplicitMap;
            expectingKey = true;
            break;
        default:
            // labels: {} nested map — treat as labels container if topKey was labels.
            if (topKey == "labels") {
                where = Where::LabelsMap;
                expectingKey = true;
            }
            break;
    }
    return true;
}

bool CascadeBuilder::OnMapEnd() {
    --depth;
    if (depth == 0) {
        // Closing cascade body
        builder->popState();
        return true;
    }
    switch (where) {
        case Where::NodeItemMap:
            // Compact-form node just finished — commit.
            if (!currentNode.name.empty()) {
                node_->nodes.push_back(std::move(currentNode));
                currentNode = CascadeNode{};
            }
            where = Where::NodesSeq;
            break;
        case Where::NodeExplicitMap:
            where = Where::NodeItemMap;
            expectingKey = true;
            break;
        case Where::LabelsMap:
            where = Where::TopLevel;
            expectingKey = true;
            break;
        default:
            expectingKey = true;
            break;
    }
    return true;
}

bool CascadeBuilder::OnSequenceStart() {
    switch (where) {
        case Where::TopLevel:
            if (topKey == "nodes") { where = Where::NodesSeq; }
            break;
        case Where::NodesSeq:
            // List-form node item `- [opcode-member, TypeA, TypeB]` (no map key).
            where = Where::NodeListSeq;
            currentNode = CascadeNode{};
            compactPos = 0;
            break;
        case Where::NodeItemMap:
            // Compact-form value list `[id, TypeA, TypeB]`
            where = Where::NodeCompactSeq;
            compactPos = 0;
            break;
        case Where::NodeExplicitMap:
            if (topKey == "fields") { where = Where::NodeFieldsSeq; }
            break;
        case Where::NodeFieldsSeq:
            // A single field item `[name, TypeName]` or `[name, TypeName, default]`
            where = Where::NodeFieldItem;
            currentField = CascadeField{};
            fieldItemPos = 0;
            break;
        default:
            break;
    }
    return true;
}

bool CascadeBuilder::OnSequenceEnd() {
    switch (where) {
        case Where::NodesSeq:
            where = Where::TopLevel;
            expectingKey = true;
            break;
        case Where::NodeCompactSeq:
            // Compact form finished — node stays in NodeItemMap until MapEnd
            where = Where::NodeItemMap;
            break;
        case Where::NodeListSeq:
            // List-form node finished — commit directly (no enclosing map).
            if (!currentNode.name.empty() || currentNode.id >= 0) {
                node_->nodes.push_back(std::move(currentNode));
                currentNode = CascadeNode{};
            }
            where = Where::NodesSeq;
            break;
        case Where::NodeFieldsSeq:
            where = Where::NodeExplicitMap;
            expectingKey = true;
            break;
        case Where::NodeFieldItem:
            // Field item done — commit
            if (!currentField.typeName.empty()) {
                currentNode.fields.push_back(std::move(currentField));
                currentField = CascadeField{};
            }
            where = Where::NodeFieldsSeq;
            break;
        default:
            break;
    }
    return true;
}

bool CascadeBuilder::OnScalar(const std::string& value, const std::string& /*tag*/) {
    switch (where) {
        case Where::TopLevel:
            if (expectingKey) {
                topKey = value;
                expectingKey = false;
            } else {
                if (topKey == "name") { node_->name = value; }
                else if (topKey == "opcode") { node_->opcode = value; }
                else if (topKey == "level") {   // spec/65 F65-19
                    int parsed = aiml::clearance::kAbsent;
                    if (aiml::clearance::parseLevel(value, "cascade " + node_->name, parsed))
                        node_->level = parsed;
                }
                expectingKey = true;
            }
            return true;

        case Where::NodesSeq:
            // Should not receive scalar at this level (each item is a map).
            return true;

        case Where::NodeItemMap:
            if (expectingKey) {
                currentNode.name = value;
                expectingKey = false;
            } else {
                // Should be handled by NodeCompactSeq or NodeExplicitMap; ignore.
            }
            return true;

        case Where::NodeListSeq:
            // List form `- [head, T1, T2]` — the node has no map-key name, so the
            // leading element IS the head: numeric → id directly; non-numeric → an
            // opcode enum member (`EffectCode.deal_damage` or bare `deal_damage`),
            // which sets the name (id resolved from `opcode:` at registration).
            if (compactPos == 0) {
                bool numeric = false;
                try {
                    size_t consumed = 0;
                    int v = std::stoi(value, &consumed);
                    if (consumed == value.size()) { currentNode.id = v; numeric = true; }
                } catch (...) { /* non-numeric */ }
                if (!numeric) {
                    std::string mem = value;
                    auto dot = mem.rfind('.');
                    if (dot != std::string::npos) mem = mem.substr(dot + 1);
                    currentNode.name = mem;
                }
            } else {
                CascadeField f;
                f.typeName = value;
                currentNode.fields.push_back(std::move(f));
            }
            ++compactPos;
            return true;

        case Where::NodeCompactSeq:
            // Compact form `- name: [ ... ]` — the map key already gave the name.
            // A numeric leading element is the explicit id; a NON-numeric leading
            // element is field 0 (the id is then resolved from the name via
            // `opcode:` at registration).
            if (compactPos == 0) {
                bool numericId = false;
                try {
                    size_t consumed = 0;
                    int v = std::stoi(value, &consumed);
                    if (consumed == value.size()) { currentNode.id = v; numericId = true; }
                } catch (...) { /* non-numeric → field 0 */ }
                if (!numericId) {
                    CascadeField f;
                    f.typeName = value;
                    currentNode.fields.push_back(std::move(f));
                }
            } else {
                CascadeField f;
                f.typeName = value;
                currentNode.fields.push_back(std::move(f));
            }
            ++compactPos;
            return true;

        case Where::NodeExplicitMap:
            if (expectingKey) {
                topKey = value; // reuse topKey for nested map key
                expectingKey = false;
            } else {
                if (topKey == "id") {
                    try { currentNode.id = std::stoi(value); }
                    catch (...) { LOG_ERROR(builder->fmtLoc() + "cascade: expected numeric id for '" + currentNode.name + "' (spec/57)"); }
                } else if (topKey == "label") {
                    currentNode.label = value;
                }
                expectingKey = true;
            }
            return true;

        case Where::NodeFieldItem:
            if (fieldItemPos == 0)      currentField.name = value;
            else if (fieldItemPos == 1) currentField.typeName = value;
            else if (fieldItemPos == 2) currentField.defaultValue = value;
            ++fieldItemPos;
            return true;

        case Where::LabelsMap:
            if (expectingKey) {
                currentLabelKey = value;
                expectingKey = false;
            } else {
                node_->labelOverrides[currentLabelKey] = value;
                currentLabelKey.clear();
                expectingKey = true;
            }
            return true;

        default:
            return true;
    }
}

std::unique_ptr<Node> CascadeBuilder::finish() {
    // Apply label overrides now that everything is parsed.
    for (auto& n : node_->nodes) {
        auto it = node_->labelOverrides.find(n.name);
        if (it != node_->labelOverrides.end() && n.label.empty()) {
            n.label = it->second;
        }
    }
    return std::move(node_);
}

} // namespace AST
} // namespace AIML
