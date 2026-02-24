#include "../../../logger.h"
#include "../ast_builder.h"
#include "enum_builder.h"

namespace AIML {
namespace AST {

bool EnumBuilder::OnMapStart() {
    expectingKey = true;
    return true;
}
bool EnumBuilder::OnMapEnd() {
    // If inside values map: `Green: 1`
    if (insideValues) {
        if (!activeValueName.empty() && overrideValue != -1) {
             enumNode->values.push_back({activeValueName, overrideValue});
        }
        activeValueName = ""; overrideValue = -1;
        return true; 
    }
    builder->popState();
    return true;
}
bool EnumBuilder::OnScalar(const std::string& value, const std::string& tag) {
    if (expectingKey) {
        currentKey = value;
        expectingKey = false;
        if (insideValues) { activeValueName = value; expectingKey = false; }
        return true;
    }
    if (currentKey == "name") enumNode->name = value;
    else if (insideValues) {
        if (!activeValueName.empty()) { overrideValue = std::stoi(value); }
        else { enumNode->values.push_back({value, -1}); }
    }
    if (!insideValues) expectingKey = true;
    return true;
}
bool EnumBuilder::OnSequenceStart() {
    if (currentKey == "values") insideValues = true;
    return true;
}
bool EnumBuilder::OnSequenceEnd() {
    if (insideValues) { insideValues = false; expectingKey = true; }
    return true;
}
std::unique_ptr<Node> EnumBuilder::finish() { 
    // Fix implicit values
    int current = 0;
    for (auto& pair : enumNode->values) {
         if (pair.second != -1) current = pair.second;
         else pair.second = current;
         current++;
    }
    return std::move(enumNode); 
}

} // namespace AST
} // namespace AIML
