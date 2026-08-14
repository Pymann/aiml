#pragma once
#include <memory>
#include <string>
#include <vector>
#include "../ast_nodes.h"
#include "../ast_builder.h"

namespace AIML {
namespace AST {

class Builder;

/// Parser for top-level `- cascade:` blocks.
///
/// Compact node form:
///   nodes:
///     - draw_card:    [1, DeckType, ValueType]
///     - counter_trap: [48]
///
/// The first element of each inner sequence is the numeric bytecode id
/// (int); the rest are field-type names.  Field-names are auto-generated
/// from type names (compiler resolves at semantic-analysis time).
///
/// Explicit form for full control (accepted but optional):
///   nodes:
///     - modify_stat:
///         id: 12
///         fields:
///           - [owner,    OwnerType]
///           - [attr,     AttributeType]
///           - [value,    ValueType]
///           - [duration, DurationType]
///
/// Optional cascade-level `labels:` map (node-name → display label):
///   labels:
///     draw_card:   "Draw Card"
///     counter_trap: "Counter Trap"
class CascadeBuilder : public StateHandler {
    // Base is StateHandler (from ast_builder.h).
public:
    CascadeBuilder(Builder* b);
    bool OnMapStart() override;
    bool OnMapEnd() override;
    bool OnScalar(const std::string& value, const std::string& tag) override;
    bool OnSequenceStart() override;
    bool OnSequenceEnd() override;
    std::unique_ptr<Node> finish() override;

private:
    // Where in the YAML tree we currently are.
    enum class Where {
        TopLevel,       // just entered cascade
        NodesSeq,       // inside nodes: [
        NodeItemMap,    // inside a single { name: {...} } item
        NodeCompactSeq, // inside `name: [id, T1, T2]`
        NodeListSeq,    // inside `- [opcode-member, T1, T2]` (name-less list form)
        NodeExplicitMap,// inside `name: { id: N, fields: [...] }`
        NodeFieldsSeq,  // inside fields: [
        NodeFieldItem,  // inside a single [name, type] or [name, type, default]
        LabelsMap,      // inside labels: { ... }
    };
    Where where = Where::TopLevel;
    int depth = 0;                     // total map-depth from cascade-open

    std::unique_ptr<CascadeDecl> node_;

    // Scratch state:
    bool expectingKey = true;
    std::string topKey;                // "name" / "nodes" / "labels"
    std::string currentNodeName;
    std::string currentLabelKey;       // key inside labels:
    CascadeNode currentNode;
    CascadeField currentField;
    int fieldItemPos = 0;              // 0=name, 1=type, 2=default
    int compactPos = 0;                // 0=id, 1..=field types
};

} // namespace AST
} // namespace AIML
