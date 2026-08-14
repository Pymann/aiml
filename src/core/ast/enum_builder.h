#pragma once
#include <memory>
#include <string>
#include <vector>
#include <stack>
#include "../ast_nodes.h"

namespace AIML {
namespace AST {

class Builder;
class StateHandler;

/// Parses top-level `- enum:` blocks.  Supported forms:
///
///   Compact:
///     - enum: { name: GamePhase, values: [start, draw, mana, spell, combat, end] }
///
///   Compact with per-id label override map:
///     - enum:
///         name: AttributeType
///         values: [lp, atk, def, ini, mov, mor]
///         labels: {1: "LP", 2: "ATK", 3: "DEF", 4: "INI", 5: "MOV", 6: "MOR"}
///
///   Legacy compact-with-override (name→id):
///     - enum: { name: Color, values: [{Red: 5}, {Green: 8}] }
///
///   Explicit full form per value:
///     - enum:
///         name: OwnerType
///         values:
///           - {id: 1, name: player,     label: "Player"}
///           - {id: 2, name: opponent,   label: "Opponent"}
///           - {id: 4, name: all_allies, label: "All Allies"}
class EnumBuilder : public StateHandler {
    std::unique_ptr<EnumDecl> enumNode;
    std::string currentKey;
    bool expectingKey = false;
    bool insideValues = false;
    bool insideLabels = false;
    int overrideValue = -1;
    std::string activeValueName;
    // Full-form value tracking (`- {id: N, name: X, label: L}`)
    bool inFullValueMap = false;
    int fullValueDepth = 0;      // nested map depth inside values
    std::string fullValueKey;
    int fullValueId = -1;
    std::string fullValueName;
    std::string fullValueLabel;
    // Labels map: key currently expected
    std::string currentLabelKey;
public:
    EnumBuilder(Builder* b) : StateHandler(b) { enumNode = std::make_unique<EnumDecl>(); }
    bool OnMapStart() override;
    bool OnMapEnd() override;
    bool OnScalar(const std::string& value, const std::string& tag) override;
    bool OnSequenceStart() override;
    bool OnSequenceEnd() override;
    std::unique_ptr<Node> finish() override;
};

} // namespace AST
} // namespace AIML
