#include "../../../logger.h"
#include "../ast_builder.h"
#include "interface_builder.h"

namespace AIML {
namespace AST {

bool InterfaceBuilder::OnMapStart() {
    expectingKey = true;
    return true;
}
bool InterfaceBuilder::OnMapEnd() {
    builder->popState();
    return true;
}
bool InterfaceBuilder::OnScalar(const std::string& value, const std::string& tag) {
    if (expectingKey) {
        currentKey = value;
        expectingKey = false;
        return true;
    }
    if (currentKey == "name") interfaceNode->name = value;
    else if (insideMethodDef) {
        if (currentMethod.name.empty()) currentMethod.name = value;
        else if (currentMethod.returnType.empty()) currentMethod.returnType = value;
        // arguments would be in next sequence
    }
    if (!insideMethods) expectingKey = true;
    return true;
}
bool InterfaceBuilder::OnSequenceStart() {
    if (currentKey == "methods") insideMethods = true;
    else if (insideMethods) { insideMethodDef = true; currentMethod.name = ""; currentMethod.returnType = ""; }
    return true;
}
bool InterfaceBuilder::OnSequenceEnd() {
    if (insideMethodDef) {
        if (!currentMethod.name.empty()) {
            interfaceNode->methods.push_back(currentMethod);
        }
        insideMethodDef = false;
    } else if (insideMethods) {
        insideMethods = false;
        expectingKey = true;
    }
    return true;
}
std::unique_ptr<Node> InterfaceBuilder::finish() { return std::move(interfaceNode); }

} // namespace AST
} // namespace AIML
