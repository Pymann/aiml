#pragma once
#include <memory>
#include <string>
#include "../ast_nodes.h"
#include "../ast_builder.h"

namespace AIML {
namespace AST {

class Builder;

/// Parser for top-level `- classgroup:` blocks (spec/58 — static component
/// wiring group).
///
///   - classgroup:
///       name: MatchRuntime
///       instances:
///         - class: CombatLog
///           instance: log
///         - class: DamageResolver
///           instance: resolver
///           sees: [log]
///           functions:
///             - def: { name: apply, ... }
///
/// Each instance desugars (in synthesizeClassGroups) to a subclass of its base
/// with sees→members + functions→methods, plus a synthesized container class.
class ClassGroupBuilder : public StateHandler {
public:
    ClassGroupBuilder(Builder* b)
        : StateHandler(b), node_(std::make_unique<ClassGroupDecl>()) {}
    bool OnMapStart() override;
    bool OnMapEnd() override;
    bool OnScalar(const std::string& value, const std::string& tag) override;
    bool OnSequenceStart() override;
    bool OnSequenceEnd() override;
    void receiveChildNode(std::unique_ptr<Node> node) override;
    std::unique_ptr<Node> finish() override;

private:
    std::unique_ptr<ClassGroupDecl> node_;
    int  depth = 0;                 // map depth: 1 = group body, 2 = instance, 3 = fn-element
    bool expectingKey = true;
    std::string currentKey;
    bool inInstances  = false;      // inside `instances:` sequence
    bool inInstanceMap = false;     // inside one instance's map
    bool inSees       = false;      // inside an instance's `sees:` sequence
    bool inImplements = false;      // inside an instance's `implements:` list
    bool inFunctions  = false;      // inside an instance's `functions:` sequence
    bool inCtor       = false;      // inside an instance's `constructor:` list
    bool inParams     = false;      // inside the group-level `params:` sequence
    int  paramsDepth  = 0;          // 1 = outer params seq, 2 = one [name,type] pair
    int  paramField   = 0;          // 0 = expect name, 1 = expect type
    std::pair<std::string, std::string> curParam;  // param pair under construction
    bool inSubgroups  = false;      // inside the group-level `subgroups:` sequence
    int  subgDepth    = 0;          // 1 = outer subgroups seq, 2 = one [name,type] pair
    int  subgField    = 0;          // 0 = expect name, 1 = expect group-type
    std::pair<std::string, std::string> curSubg;   // subgroup pair under construction
    bool inExports    = false;      // inside the group-level `export:` sequence
    ClassGroupInstance cur_;        // instance under construction
};

} // namespace AST
} // namespace AIML
