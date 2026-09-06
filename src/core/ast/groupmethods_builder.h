#pragma once
#include <memory>
#include <string>
#include "../ast_nodes.h"
#include "../ast_builder.h"

namespace AIML {
namespace AST {

class Builder;

/// Parser for top-level `- groupmethods:` blocks (spec/58 — external
/// coordination methods).  Splits an instance's `methods:` out of the classgroup
/// body into its own file:
///
///   - groupmethods:
///       group: ServerRuntime          # target classgroup
///       instances:
///         - instance: phaseco          # target instance
///           methods:
///             - def: { name: confirm_phase, ... }   # uses <.matches (sees member)
///
/// Merged into the target ClassGroupInstance.functions before synthesizeClassGroups,
/// so the methods land on the synthesized subclass with the full cross-edge API.
class GroupMethodsBuilder : public StateHandler {
public:
    GroupMethodsBuilder(Builder* b)
        : StateHandler(b), node_(std::make_unique<GroupMethodsDecl>()) {}
    bool OnMapStart() override;
    bool OnMapEnd() override;
    bool OnScalar(const std::string& value, const std::string& tag) override;
    bool OnSequenceStart() override;
    bool OnSequenceEnd() override;
    void receiveChildNode(std::unique_ptr<Node> node) override;
    std::unique_ptr<Node> finish() override;

private:
    std::unique_ptr<GroupMethodsDecl> node_;
    int  depth = 0;                 // map depth: 1 = body, 2 = instance, 3 = method-element
    bool expectingKey = true;
    std::string currentKey;
    bool inInstances   = false;     // inside `instances:` sequence
    bool inInstanceMap = false;     // inside one instance's map
    bool inMethods     = false;     // inside an instance's `methods:` sequence
    GroupMethodsTarget cur_;        // instance-target under construction
};

} // namespace AST
} // namespace AIML
