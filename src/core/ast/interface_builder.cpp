#include "../../../logger.h"
#include "../ast_builder.h"
#include "interface_builder.h"

namespace AIML {
namespace AST {

bool InterfaceBuilder::OnMapStart() {
    // spec/61 §3.1: a { lifetime: consumed } modifier map as the 3rd element of an arg pair.
    if (insideMethods && methodDepth == 3) {
        inArgModMap = true;
        argModExpectKey = true;
        return true;
    }
    expectingKey = true;
    return true;
}
bool InterfaceBuilder::OnMapEnd() {
    if (inArgModMap) { inArgModMap = false; return true; }
    builder->popState();
    return true;
}
bool InterfaceBuilder::OnScalar(const std::string& value, const std::string& tag) {
    if (inArgModMap) {
        if (argModExpectKey) { argModKey = value; argModExpectKey = false; }
        else {
            if (argModKey == "lifetime" &&
                (value == "scope" || value == "persistent" || value == "consumed"))
                curArgLifetime = value;
            argModExpectKey = true;
        }
        return true;
    }
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
        } else if (methodDepth == 3) {
            // Inner [argName, argType] sub-sequence.
            if (!curArgGotName) {
                currentArg.first = value;
                curArgGotName = true;
            } else {
                currentArg.second = value;
            }
        }
        // methodDepth == 2 → args sub-sequence container, scalars there ignored
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
        } else if (methodDepth == 3) {
            // Start of an [argName, argType] pair inside args.
            currentArg = {};
            curArgGotName = false;
            curArgLifetime = "scope";
        }
        // depth == 2: args sub-sequence container
    }
    return true;
}
bool InterfaceBuilder::OnSequenceEnd() {
    if (insideMethods && methodDepth > 0) {
        if (methodDepth == 3 && curArgGotName && !currentArg.first.empty()) {
            // End of [argName, argType] pair.
            currentMethod.args.push_back(currentArg);
            currentMethod.argLifetimes.push_back(curArgLifetime);  // parallel to args (spec/61)
        }
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
