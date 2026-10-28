// groupinterface_builder.cpp — see groupinterface_builder.h (spec/58 §12).
#include "groupinterface_builder.h"
#include "../ast_builder.h"
#include "../clearance.h"
#include "../../../logger.h"

#include <set>

namespace AIML {
namespace AST {

bool GroupInterfaceBuilder::OnMapStart() { expectingKey = true; return true; }

bool GroupInterfaceBuilder::OnMapEnd() {
    builder->popState();
    return true;
}

bool GroupInterfaceBuilder::OnSequenceStart() {
    if (exportsDepth == 0 && currentKey == "exports") { exportsDepth = 1; return true; }
    if (exportsDepth == 1) { exportsDepth = 2; field = 0; cur = {}; return true; }
    return true;
}

bool GroupInterfaceBuilder::OnSequenceEnd() {
    if (exportsDepth == 2) {
        if (!cur.first.empty()) node_->groupExports.push_back(cur);
        exportsDepth = 1;
        return true;
    }
    if (exportsDepth == 1) { exportsDepth = 0; expectingKey = true; return true; }
    return true;
}

bool GroupInterfaceBuilder::OnScalar(const std::string& value, const std::string& /*tag*/) {
    if (exportsDepth == 2) {
        if (field == 0) { cur.first = value; field = 1; }
        else            { cur.second = value; field = 2; }
        return true;
    }
    if (expectingKey) { currentKey = value; expectingKey = false; return true; }
    if (currentKey == "name") node_->name = value;
    else if (currentKey == "level") {   // spec/65 F65-19
        int parsed = aiml::clearance::kAbsent;
        if (aiml::clearance::parseLevel(value, "groupinterface " + node_->name, parsed))
            node_->level = parsed;
    }
    expectingKey = true;
    return true;
}

std::unique_ptr<Node> GroupInterfaceBuilder::finish() {
    // §12.3: one accessor per export (first spelling wins; a duplicate is GI-3,
    // reported by checkGroupInterfaces from groupExports).
    std::set<std::string> seen;
    for (const auto& ex : node_->groupExports) {
        if (!seen.insert(ex.first).second) continue;
        MethodSignature sig;
        sig.name = "__gi_" + ex.first;
        sig.returnType = "ref<" + ex.second + ">";
        node_->methods.push_back(sig);
    }
    return std::move(node_);
}

}  // namespace AST
}  // namespace AIML
