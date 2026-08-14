#include "alias_builder.h"
#include "../ast_builder.h"
#include "../../../logger.h"

namespace AIML {
namespace AST {

bool AliasBuilder::OnMapStart() {
    ++depth;
    expectingKey = true;
    return true;
}

bool AliasBuilder::OnMapEnd() {
    --depth;
    if (depth == 0) builder->popState();
    return true;
}

bool AliasBuilder::OnScalar(const std::string& value, const std::string& /*tag*/) {
    if (expectingKey) {
        currentKey = value;
        expectingKey = false;
        return true;
    }
    if      (currentKey == "name")    node_->name         = value;
    else if (currentKey == "type")    node_->typeName     = value;
    else if (currentKey == "label")   node_->label        = value;
    else if (currentKey == "min")     node_->minValue     = value;
    else if (currentKey == "max")     node_->maxValue     = value;
    else if (currentKey == "default") node_->defaultValue = value;
    expectingKey = true;
    return true;
}

std::unique_ptr<Node> AliasBuilder::finish() {
    return std::move(node_);
}

} // namespace AST
} // namespace AIML
