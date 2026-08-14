#pragma once
#include <memory>
#include <string>
#include "../ast_nodes.h"
#include "../ast_builder.h"

namespace AIML {
namespace AST {

class Builder;

/// Parser for top-level `- alias:` blocks:
///
///     - alias: { name: VALUE, type: i32, label: "Value", min: 0 }
///     - alias: { name: SIDES, type: i32, label: "Sides", min: 2, default: 6 }
///
/// All fields are simple scalars in a single flow-map — the parser reads
/// key/value pairs and stores them until OnMapEnd.
class AliasBuilder : public StateHandler {
    std::unique_ptr<AliasDecl> node_;
    std::string currentKey;
    bool expectingKey = true;
    int depth = 0;
public:
    AliasBuilder(Builder* b) : StateHandler(b), node_(std::make_unique<AliasDecl>()) {}
    bool OnMapStart() override;
    bool OnMapEnd() override;
    bool OnScalar(const std::string& value, const std::string& tag) override;
    std::unique_ptr<Node> finish() override;
};

} // namespace AST
} // namespace AIML
