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
    if (insideMethodDef) {
        if (methodDepth == 1) {
            // Outer [name, ret, args] — first two scalars.
            if (currentMethod.name.empty()) currentMethod.name = value;
            else if (currentMethod.returnType.empty()) currentMethod.returnType = value;
        }
        // methodDepth == 2 → args sub-sequence (skipped for now)
    } else if (currentKey == "name") {
        interfaceNode->name = value;
    }
    if (!insideMethods) expectingKey = true;
    return true;
}
bool InterfaceBuilder::OnSequenceStart() {
    if (!insideMethods && currentKey == "methods") {
        // Outer "methods: -" array begins
        insideMethods = true;
    } else if (insideMethods) {
        methodDepth++;
        if (methodDepth == 1) {
            insideMethodDef = true;
            currentMethod = MethodSignature{};
        }
        // depth >= 2: inner sub-sequence (the args [])
    }
    return true;
}
bool InterfaceBuilder::OnSequenceEnd() {
    if (insideMethods && methodDepth > 0) {
        methodDepth--;
        if (methodDepth == 0 && insideMethodDef) {
            if (!currentMethod.name.empty())
                interfaceNode->methods.push_back(currentMethod);
            insideMethodDef = false;
        }
    } else if (insideMethods) {
        // End of outer methods array
        insideMethods = false;
        expectingKey = true;
    }
    return true;
}
std::unique_ptr<Node> InterfaceBuilder::finish() { return std::move(interfaceNode); }

} // namespace AST
} // namespace AIML
