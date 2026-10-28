#include "../clearance.h"
#include "classgroup_builder.h"
#include "function_builder.h"
#include "../ast_builder.h"
#include "../../../logger.h"

namespace AIML {
namespace AST {

// Map depths (only maps count toward `depth`; sequences do not):
//   1 = the group body            { name:, instances: }
//   2 = one instance's map        { class:, instance:, sees:, methods: }
//   3 = a `methods:` element       { def: ... }   ("def" then delegates)
bool ClassGroupBuilder::OnMapStart() {
    ++depth;
    // Optional 3rd element of a `subgroups:` entry — a `{port: inst}` binding map
    // (`[fx, EffectRuntime, {cards: cards}]`).  Its scalars are key/value pairs.
    if (inSubgroups && subgDepth == 2) {
        inSubgPortMap = true;
        subgPortKey = true;
        expectingKey = true;
        return true;
    }
    if (depth == 2 && inInstances && !inFunctions) {
        // Start a fresh instance.
        inInstanceMap = true;
        cur_ = ClassGroupInstance{};
    }
    // depth==3 (a functions-list `- def:` element) needs no state beyond a
    // key expectation for the upcoming "def" scalar.
    expectingKey = true;
    return true;
}

bool ClassGroupBuilder::OnMapEnd() {
    if (inSubgPortMap) {
        // Close the subgroup's port-binding map; store under the subgroup name.
        node_->subgroupPorts[curSubg.first] = curSubgPorts;
        curSubgPorts.clear();
        inSubgPortMap = false;
        --depth;
        return true;
    }
    if (depth == 2 && inInstanceMap) {
        node_->instances.push_back(std::move(cur_));
        inInstanceMap = false;
    }
    --depth;
    if (depth == 0) builder->popState();   // group body closed
    return true;
}

bool ClassGroupBuilder::OnSequenceStart() {
    // Group-level `params: [[name, type], ...]` — a sequence of 2-element pairs.
    if (inParams) { paramsDepth = 2; paramField = 0; curParam = {}; return true; }
    if (!inInstanceMap && currentKey == "params") { inParams = true; paramsDepth = 1; return true; }
    // Group-level `requires: [[port, BaseClass], ...]` — same pair shape (ports).
    if (inPorts) { portsDepth = 2; portField = 0; curPort = {}; return true; }
    if (!inInstanceMap && currentKey == "requires") { inPorts = true; portsDepth = 1; return true; }
    // Group-level `subgroups: [[name, GroupType], ...]` — same pair shape.
    if (inSubgroups) { subgDepth = 2; subgField = 0; curSubg = {}; return true; }
    if (!inInstanceMap && currentKey == "subgroups") { inSubgroups = true; subgDepth = 1; return true; }
    // Group-level `export: [inst, ...]` — a flat string list.
    if (!inInstanceMap && currentKey == "export") { inExports = true; return true; }
    // Group-level `implements: [GroupIface, ...]` (spec/58 §12) — a flat list.
    if (!inInstanceMap && !inInstances && currentKey == "implements") { inGroupImplements = true; return true; }

    if (!inInstanceMap && currentKey == "instances") inInstances = true;
    else if (inInstanceMap && currentKey == "sees")        inSees = true;
    else if (inInstanceMap && currentKey == "implements")  inImplements = true;
    // `methods:` is the canonical instance-level key (coordination methods lifted
    // onto the synthesized subclass); `functions:` is a legacy alias.
    else if (inInstanceMap && (currentKey == "methods" ||
                               currentKey == "functions")) inFunctions = true;
    else if (inInstanceMap && currentKey == "constructor") inCtor = true;
    return true;
}

bool ClassGroupBuilder::OnSequenceEnd() {
    // params: inner pair closes → push; outer closes → leave params.
    if (inParams && paramsDepth == 2) { node_->params.push_back(curParam); paramsDepth = 1; return true; }
    if (inParams && paramsDepth == 1) { inParams = false; paramsDepth = 0; expectingKey = true; return true; }
    // requires: same pair shape.
    if (inPorts && portsDepth == 2) { node_->ports.push_back(curPort); portsDepth = 1; return true; }
    if (inPorts && portsDepth == 1) { inPorts = false; portsDepth = 0; expectingKey = true; return true; }
    // subgroups: same pair shape.
    if (inSubgroups && subgDepth == 2) { node_->subgroups.push_back(curSubg); subgDepth = 1; return true; }
    if (inSubgroups && subgDepth == 1) { inSubgroups = false; subgDepth = 0; expectingKey = true; return true; }
    if (inExports) { inExports = false; expectingKey = true; return true; }
    if (inGroupImplements) { inGroupImplements = false; expectingKey = true; return true; }
    // Innermost-first: sees/functions/constructor live inside an instance map;
    // instances wraps them.  Only the innermost flag is set at a given level.
    if (inSees)            inSees = false;
    else if (inImplements) inImplements = false;
    else if (inFunctions)  inFunctions = false;
    else if (inCtor)       inCtor = false;
    else if (inInstances)  inInstances = false;
    expectingKey = true;
    return true;
}

bool ClassGroupBuilder::OnScalar(const std::string& value, const std::string& /*tag*/) {
    // `params: [[name, type], ...]` — two scalars per pair.
    if (inParams) {
        if (paramField == 0) { curParam.first = value;  paramField = 1; }
        else                 { curParam.second = value; paramField = 2; }
        return true;
    }
    // A subgroup entry's `{port: inst}` binding map — key/value scalars.  Checked
    // before the generic `inSubgroups` branch (both flags are set inside the map).
    if (inSubgPortMap) {
        if (subgPortKey) { curSubgPortKey = value; subgPortKey = false; }
        else             { curSubgPorts.push_back({curSubgPortKey, value}); subgPortKey = true; }
        return true;
    }
    // `requires: [[port, BaseClass], ...]` — two scalars per pair.
    if (inPorts) {
        if (portField == 0) { curPort.first = value;  portField = 1; }
        else                { curPort.second = value; portField = 2; }
        return true;
    }
    // `subgroups: [[name, GroupType], ...]` — two scalars per pair (+ optional map).
    if (inSubgroups) {
        if (subgField == 0) { curSubg.first = value;  subgField = 1; }
        else                { curSubg.second = value; subgField = 2; }
        return true;
    }
    // `export: [inst, ...]` — bare scalars.
    if (inExports) { node_->exports.push_back(value); return true; }
    if (inGroupImplements) { node_->implementsGroups.push_back(value); return true; }
    // `sees: [a, b]` — bare scalars inside the sees sequence.
    if (inSees) {
        cur_.sees.push_back(value);
        return true;
    }
    // `implements: [Iface, ...]` — bare interface names.
    if (inImplements) {
        cur_.implements.push_back(value);
        return true;
    }
    // `constructor: [method, arg, ...]` — bare scalars inside the ctor list.
    if (inCtor) {
        cur_.constructor.push_back(value);
        return true;
    }
    // `methods: [- def: ...]` — delegate each `def` to a FunctionBuilder; the
    // finished FunctionDecl arrives via receiveChildNode.
    if (inFunctions && expectingKey && value == "def") {
        builder->pushState(std::make_unique<FunctionBuilder>(builder));
        expectingKey = true;   // for when FunctionBuilder returns
        return true;
    }
    if (expectingKey) {
        currentKey = value;
        expectingKey = false;
        return true;
    }
    // Scalar value for currentKey.
    if (inInstanceMap) {
        if      (currentKey == "class")       cur_.className    = value;
        else if (currentKey == "instance")    cur_.instanceName = value;
        // `constructor: method` scalar shorthand (no args).
        else if (currentKey == "constructor") cur_.constructor.push_back(value);
    } else if (depth == 1) {
        if (currentKey == "name") node_->name = value;
        else if (currentKey == "level") {   // spec/65 F65-19
            int parsed = aiml::clearance::kAbsent;
            if (aiml::clearance::parseLevel(value, "classgroup " + node_->name, parsed))
                node_->level = parsed;
        }
    }
    expectingKey = true;
    return true;
}

void ClassGroupBuilder::receiveChildNode(std::unique_ptr<Node> node) {
    // A completed `- def:` from FunctionBuilder → this instance's coordination
    // method.  (FunctionDecl is a Node subclass.)
    if (auto* fn = dynamic_cast<FunctionDecl*>(node.get())) {
        node.release();
        cur_.functions.push_back(std::unique_ptr<FunctionDecl>(fn));
    }
    expectingKey = true;
}

std::unique_ptr<Node> ClassGroupBuilder::finish() {
    return std::move(node_);
}

} // namespace AST
} // namespace AIML
