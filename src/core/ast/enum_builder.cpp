#include "../../../logger.h"
#include "../ast_builder.h"
#include "enum_builder.h"

namespace AIML {
namespace AST {

bool EnumBuilder::OnMapStart() {
    if (currentKey == "labels" && !insideValues && !insideLabels) {
        insideLabels = true;
        expectingKey = true;
        return true;
    }
    if (insideLabels) {
        // Nested map inside labels — not supported; treat as no-op.
        expectingKey = true;
        return true;
    }
    if (insideValues) {
        // A value-item map begins.  Could be legacy override `{Name: id}`
        // or full form `{id: N, name: X, label: L}`.  We don't know yet
        // which — dispatch based on the first key we see.
        fullValueDepth++;
        activeValueName = "";
        overrideValue = -1;
        inFullValueMap = false;
        fullValueId = -1;
        fullValueName.clear();
        fullValueLabel.clear();
        fullValueKey.clear();
        expectingKey = true;
        return true;
    }
    expectingKey = true;
    return true;
}
bool EnumBuilder::OnMapEnd() {
    if (insideLabels) {
        insideLabels = false;
        expectingKey = true;
        return true;
    }
    if (insideValues) {
        fullValueDepth--;
        if (inFullValueMap) {
            // Full form: commit {id, name, label} triple
            if (!fullValueName.empty() && fullValueId >= 0) {
                enumNode->values.push_back({fullValueName, fullValueId});
                if (!fullValueLabel.empty()) {
                    enumNode->labels[fullValueId] = fullValueLabel;
                }
            }
            inFullValueMap = false;
            return true;
        }
        // Legacy override form: `{Name: id}` — activeValueName + overrideValue
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
    if (insideLabels) {
        if (expectingKey) {
            currentLabelKey = value;
            expectingKey = false;
        } else {
            try {
                int id = std::stoi(currentLabelKey);
                enumNode->labels[id] = value;
            } catch (...) {
                LOG_ERROR("enum labels: expected integer id key, got '" + currentLabelKey + "'");
            }
            currentLabelKey.clear();
            expectingKey = true;
        }
        return true;
    }
    if (expectingKey) {
        currentKey = value;
        expectingKey = false;
        if (insideValues) {
            // Ambiguity: this key inside a value-item map might be a legacy
            // name (Green, Red) or a full-form key (id, name, label).
            if (value == "id" || value == "name" || value == "label") {
                inFullValueMap = true;
                fullValueKey = value;
            } else {
                activeValueName = value;
            }
            expectingKey = false;
        }
        return true;
    }
    if (currentKey == "name" && !insideValues) enumNode->name = value;
    else if (insideValues) {
        if (inFullValueMap) {
            if (fullValueKey == "id") {
                try { fullValueId = std::stoi(value); }
                catch (...) { LOG_ERROR("enum full-form: expected numeric id"); }
            } else if (fullValueKey == "name") {
                fullValueName = value;
            } else if (fullValueKey == "label") {
                fullValueLabel = value;
            }
            fullValueKey.clear();
        } else if (!activeValueName.empty()) {
            try { overrideValue = std::stoi(value); }
            catch (...) { LOG_ERROR("enum legacy-override: expected numeric id"); }
        } else {
            enumNode->values.push_back({value, -1});
        }
    }
    if (!insideValues) expectingKey = true;
    else if (inFullValueMap) expectingKey = true;
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
    // Fix implicit values (auto-numbering from 1 for entries that had no id).
    // Legacy behaviour: `values: [red, green]` gets red=0, green=1.  We
    // preserve that for backwards-compat.
    int current = 0;
    for (auto& pair : enumNode->values) {
         if (pair.second != -1) current = pair.second;
         else pair.second = current;
         current++;
    }
    return std::move(enumNode);
}

// Handle top-level `labels:` key by transitioning state.  Called from OnScalar
// when currentKey becomes "labels" at top level and the next event is
// OnMapStart.  We enter LabelsMap mode there.
// (Implemented in-line above via the currentKey check when OnMapStart fires
// and currentKey == "labels".)

} // namespace AST
} // namespace AIML
