#include "../../../logger.h"
#include "../ast_builder.h"
#include "namespace_builder.h"
#include "import_builder.h"
#include "config_builder.h"

namespace AIML {
namespace AST {

bool NamespaceBuilder::OnScalar(const std::string& value, const std::string& tag) {
    if (nsNode->name.empty()) nsNode->name = value;
    return true;
}
std::unique_ptr<Node> NamespaceBuilder::finish() { return std::move(nsNode); }

bool ImportBuilder::OnScalar(const std::string& value, const std::string& tag) {
    if (importNode->path.empty()) importNode->path = value;
    return true;
}
std::unique_ptr<Node> ImportBuilder::finish() { return std::move(importNode); }

bool ConfigBuilder::OnMapStart() {
    expectingKey = true;
    return true;
}
bool ConfigBuilder::OnMapEnd() {
    if (insideConfig) {
        insideConfig = false;
        return true;
    }
    builder->popState();
    return true;
}
bool ConfigBuilder::OnScalar(const std::string& value, const std::string& tag) {
    if (expectingKey) {
        currentKey = value;
        expectingKey = false;
        if (currentKey == "config" && !insideConfig) {
             insideConfig = true; // The value is a map, so next event is MapStart
             expectingKey = true;
        }
        return true;
    }
    if (insideConfig) {
        if (currentKey == "entrypoint") configNode->entryPoint = value;
        else if (currentKey == "stack_size") configNode->stackSize = std::stoull(value);
        else if (currentKey == "heap_size") configNode->heapSize = std::stoull(value);
        else if (currentKey == "target") configNode->target = value;
        else if (currentKey == "version") configNode->version = value;
        expectingKey = true;
    }
    return true;
}
std::unique_ptr<Node> ConfigBuilder::finish() { return std::move(configNode); }

} // namespace AST
} // namespace AIML
