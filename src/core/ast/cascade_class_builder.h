#pragma once
#include <memory>
#include <string>
#include "../ast_nodes.h"
#include "../ast_builder.h"

namespace AIML {
namespace AST {

class Builder;

/// Parser for top-level `- cascade_class:` blocks (spec/57 — function-cascade
/// method pool).
///
///   - cascade_class:
///       name: EffectText
///       binds_cascade: Effect        # OR: binds_enum: TargetType
///       carrier: Draft
///       taker:   Draft
///       members: [EffectSkeleton, OwnerRefText, ValueText]
///
/// M1a: parses the pool declaration only.  The rules/Mask live on the member
/// classes (`renders:`/`rules:`), wired in a later milestone.
class CascadeClassBuilder : public StateHandler {
public:
    CascadeClassBuilder(Builder* b)
        : StateHandler(b), node_(std::make_unique<CascadeClassDecl>()) {}
    bool OnMapStart() override;
    bool OnMapEnd() override;
    bool OnScalar(const std::string& value, const std::string& tag) override;
    bool OnSequenceStart() override;
    bool OnSequenceEnd() override;
    std::unique_ptr<Node> finish() override;

private:
    std::unique_ptr<CascadeClassDecl> node_;
    int  depth = 0;
    bool expectingKey = true;
    bool insideMembers = false;
    std::string currentKey;
};

} // namespace AST
} // namespace AIML
