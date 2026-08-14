#include "cascade_class_builder.h"
#include "../ast_builder.h"
#include "../../../logger.h"

namespace AIML {
namespace AST {

bool CascadeClassBuilder::OnMapStart() {
    ++depth;
    expectingKey = true;
    return true;
}

bool CascadeClassBuilder::OnMapEnd() {
    --depth;
    if (depth == 0) builder->popState();
    return true;
}

bool CascadeClassBuilder::OnSequenceStart() {
    if (currentKey == "members") insideMembers = true;
    return true;
}

bool CascadeClassBuilder::OnSequenceEnd() {
    if (insideMembers) {
        insideMembers = false;
        expectingKey = true;   // sequence value done → next scalar is a key
    }
    return true;
}

bool CascadeClassBuilder::OnScalar(const std::string& value, const std::string& /*tag*/) {
    if (insideMembers) {
        node_->members.push_back(value);
        return true;
    }
    if (expectingKey) {
        currentKey = value;
        expectingKey = false;
        return true;
    }
    if      (currentKey == "name")          node_->name         = value;
    else if (currentKey == "binds_cascade") node_->bindsCascade = value;
    else if (currentKey == "binds_enum")    node_->bindsEnum    = value;
    else if (currentKey == "carrier")       node_->carrier      = value;
    else if (currentKey == "taker")         node_->taker        = value;
    expectingKey = true;
    return true;
}

std::unique_ptr<Node> CascadeClassBuilder::finish() {
    return std::move(node_);
}

} // namespace AST
} // namespace AIML