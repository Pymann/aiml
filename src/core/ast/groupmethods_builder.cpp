#include "groupmethods_builder.h"
#include "function_builder.h"
#include "../ast_builder.h"
#include "../../../logger.h"

namespace AIML {
namespace AST {

// Map depths (only maps count toward `depth`; sequences do not):
//   1 = the body                  { group:, instances: }
//   2 = one instance's map        { instance:, methods: }
//   3 = a `methods:` element       { def: ... }   ("def" then delegates)
bool GroupMethodsBuilder::OnMapStart() {
    ++depth;
    if (depth == 2 && inInstances && !inMethods) {
        inInstanceMap = true;
        cur_ = GroupMethodsTarget{};
    }
    expectingKey = true;
    return true;
}

bool GroupMethodsBuilder::OnMapEnd() {
    if (depth == 2 && inInstanceMap) {
        node_->targets.push_back(std::move(cur_));
        inInstanceMap = false;
    }
    --depth;
    if (depth == 0) builder->popState();   // body closed
    return true;
}

bool GroupMethodsBuilder::OnSequenceStart() {
    if (!inInstanceMap && currentKey == "instances") inInstances = true;
    else if (inInstanceMap && currentKey == "methods") inMethods = true;
    return true;
}

bool GroupMethodsBuilder::OnSequenceEnd() {
    // Innermost-first: methods lives inside an instance map; instances wraps it.
    if (inMethods)         inMethods = false;
    else if (inInstances)  inInstances = false;
    expectingKey = true;
    return true;
}

bool GroupMethodsBuilder::OnScalar(const std::string& value, const std::string& /*tag*/) {
    // `methods: [- def: ...]` — delegate each `def` to a FunctionBuilder.
    if (inMethods && expectingKey && value == "def") {
        builder->pushState(std::make_unique<FunctionBuilder>(builder));
        expectingKey = true;
        return true;
    }
    if (expectingKey) {
        currentKey = value;
        expectingKey = false;
        return true;
    }
    // Scalar value for currentKey.
    if (inInstanceMap) {
        if (currentKey == "instance") cur_.instanceName = value;
    } else if (depth == 1) {
        if (currentKey == "group") node_->group = value;
    }
    expectingKey = true;
    return true;
}

void GroupMethodsBuilder::receiveChildNode(std::unique_ptr<Node> node) {
    if (auto* fn = dynamic_cast<FunctionDecl*>(node.get())) {
        node.release();
        cur_.methods.push_back(std::unique_ptr<FunctionDecl>(fn));
    }
    expectingKey = true;
}

std::unique_ptr<Node> GroupMethodsBuilder::finish() {
    return std::move(node_);
}

} // namespace AST
} // namespace AIML
