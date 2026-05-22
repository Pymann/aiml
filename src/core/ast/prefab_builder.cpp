#include "../../../logger.h"
#include "../ast_builder.h"
#include "prefab_builder.h"
#include <cstdlib>

namespace AIML {
namespace AST {

bool PrefabBuilder::OnMapStart() {
    mapDepth++;
    if (insideChildren && !insideChildObj) {
        /* Start of a child object inside the children sequence */
        insideChildObj = true;
        currentChild = PrefabChildInfo();
        childExpectingKey = true;
        return true;
    }
    if (mapDepth == 1) {
        expectingKey = true;
    }
    return true;
}

bool PrefabBuilder::OnMapEnd() {
    mapDepth--;
    if (insideChildObj) {
        /* Finished parsing one child object */
        prefabNode->children.push_back(std::move(currentChild));
        insideChildObj = false;
        return true;
    }
    if (mapDepth <= 0) {
        builder->popState();
    }
    return true;
}

static uint32_t parse_color(const std::string& s) {
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        return (uint32_t)strtoul(s.c_str(), nullptr, 16);
    return (uint32_t)strtoul(s.c_str(), nullptr, 10);
}

bool PrefabBuilder::OnScalar(const std::string& value, const std::string& tag) {
    if (insideChildObj) {
        if (childExpectingKey) {
            childKey = value;
            childExpectingKey = false;
        } else {
            if (childKey == "name")      currentChild.name = value;
            else if (childKey == "type") currentChild.type = value;
            else if (childKey == "texture") currentChild.texture = value;
            else if (childKey == "anchor")  currentChild.anchor = value;
            else if (childKey == "x")         currentChild.x = strtof(value.c_str(), nullptr);
            else if (childKey == "y")         currentChild.y = strtof(value.c_str(), nullptr);
            else if (childKey == "width")     currentChild.width = strtof(value.c_str(), nullptr);
            else if (childKey == "height")    currentChild.height = strtof(value.c_str(), nullptr);
            else if (childKey == "font_size") currentChild.font_size = strtof(value.c_str(), nullptr);
            else if (childKey == "color")     currentChild.color = parse_color(value);
            /* UI-widget style fields */
            else if (childKey == "bg_color")      currentChild.bg_color = parse_color(value);
            else if (childKey == "border_color")  currentChild.border_color = parse_color(value);
            else if (childKey == "border_width")  currentChild.border_width = strtof(value.c_str(), nullptr);
            else if (childKey == "corner_radius") currentChild.corner_radius = strtof(value.c_str(), nullptr);
            else if (childKey == "hover_color")   currentChild.hover_color = parse_color(value);
            else if (childKey == "active_color")  currentChild.active_color = parse_color(value);
            else if (childKey == "label")         currentChild.label = value;
            else if (childKey == "secret")        currentChild.is_secret = (value == "true" || value == "1");
            else if (childKey == "on_click")      currentChild.on_click = value;
            else if (childKey == "on_change")     currentChild.on_change = value;
            else if (childKey == "material")      currentChild.material = value;
            childExpectingKey = true;
        }
        return true;
    }

    if (expectingKey) {
        currentKey = value;
        expectingKey = false;
        return true;
    }

    if (currentKey == "name")      prefabNode->name = value;
    else if (currentKey == "width")  prefabNode->width = strtof(value.c_str(), nullptr);
    else if (currentKey == "height") prefabNode->height = strtof(value.c_str(), nullptr);
    else if (currentKey == "texture") prefabNode->texture = value;
    else if (currentKey == "draggable") {
        prefabNode->draggable = (value == "true" || value == "1");
    }

    expectingKey = true;
    return true;
}

bool PrefabBuilder::OnSequenceStart() {
    if (currentKey == "children") {
        insideChildren = true;
        currentKey = "";
    }
    return true;
}

bool PrefabBuilder::OnSequenceEnd() {
    if (insideChildren) {
        insideChildren = false;
        expectingKey = true;
    }
    return true;
}

void PrefabBuilder::receiveChildNode(std::unique_ptr<Node> node) { }
std::unique_ptr<Node> PrefabBuilder::finish() { return std::move(prefabNode); }

} // namespace AST
} // namespace AIML
