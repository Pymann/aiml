#include "../../../logger.h"
#include "../ast_builder.h"
#include "prefab_builder.h"
#include "../asset_scanner.h"
#include <cstdlib>

namespace {
// Spec/22 + backlog 2026-06-07: prefab texture/shader/font fields are STRICT
// stem-only.  Path-shaped values → compile-error.  Unknown stems → compile-error
// (typo catched at build-time, not as black-square at runtime).
void validate_asset_field(const std::string& field, const std::string& value,
                          AIML::AssetKind kind) {
    if (value.empty()) return;
    if (value.find('/') != std::string::npos) {
        LOG_ERROR(std::string("prefab ") + field +
                  " must be a stem, not a path: '" + value + "'");
        return;
    }
    if (value.find('.') != std::string::npos) {
        LOG_ERROR(std::string("prefab ") + field +
                  " must be a stem (no extension): '" + value + "'");
        return;
    }
    if (!AIML::AssetScanner::instance().has_stem(kind, value)) {
        LOG_ERROR(std::string("prefab ") + field +
                  ": stem '" + value + "' not in " +
                  AIML::AssetScanner::kind_name(kind) + " store");
    }
}
}  // namespace

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
            else if (childKey == "texture") {
                validate_asset_field("texture", value, AIML::AssetKind::Texture);
                currentChild.texture = value;
            }
            else if (childKey == "anchor")  currentChild.anchor = value;
            else if (childKey == "valign")  currentChild.valign = value;
            // `x`/`y` are legacy; accept `pos_x`/`pos_y` as aliases so YAML
            // prefab children comply with the AIML ≥3-char naming-lint.
            else if (childKey == "x" || childKey == "pos_x") currentChild.x = strtof(value.c_str(), nullptr);
            else if (childKey == "y" || childKey == "pos_y") currentChild.y = strtof(value.c_str(), nullptr);
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
            else if (childKey == "outline_color") currentChild.outline_color = parse_color(value);
            else if (childKey == "outline_px")    currentChild.outline_px = strtof(value.c_str(), nullptr);
            else if (childKey == "bg_texture")    currentChild.bg_texture = value;
            else if (childKey == "tile_size")     currentChild.tile_size = strtof(value.c_str(), nullptr);
            else if (childKey == "track_texture")        currentChild.track_texture = value;
            else if (childKey == "handle_texture")       currentChild.handle_texture = value;
            else if (childKey == "handle_gem_texture")   currentChild.handle_gem_texture = value;
            else if (childKey == "frame_border_texture") currentChild.frame_border_texture = value;
            else if (childKey == "frame_border_size")    currentChild.frame_border_size = strtof(value.c_str(), nullptr);
            else if (childKey == "flip_h")             currentChild.flip_h = (value == "true" || value == "1");
            else if (childKey == "snap")               currentChild.slider_snap = strtof(value.c_str(), nullptr);
            else if (childKey == "label")         currentChild.label = value;
            else if (childKey == "secret")        currentChild.is_secret = (value == "true" || value == "1");
            else if (childKey == "value")         currentChild.bar_value = strtof(value.c_str(), nullptr);
            else if (childKey == "max")           currentChild.bar_max = strtof(value.c_str(), nullptr);
            else if (childKey == "fill_color")    currentChild.fill_color = parse_color(value);
            else if (childKey == "on_click")      currentChild.on_click = value;
            else if (childKey == "on_change")     currentChild.on_change = value;
            else if (childKey == "group")         currentChild.group = value;
            else if (childKey == "options")       currentChild.options = value;
            else if (childKey == "material")      currentChild.material = value;
            /* Phase-6 model binding + container attributes */
            else if (childKey == "bind")          currentChild.bind = value;
            else if (childKey == "bind_list")     currentChild.bind_list = value;
            else if (childKey == "spawn")         currentChild.spawn = value;
            else if (childKey == "layout")        currentChild.layout = value;
            else if (childKey == "gap")           currentChild.gap = strtof(value.c_str(), nullptr);
            else if (childKey == "compact")       currentChild.compact = (value == "true" || value == "1");
            else if (childKey == "cascade_type")  currentChild.cascade_type = value;
            else if (childKey == "payload_spawn") currentChild.payload_spawn = value;
            else if (childKey == "enum")          currentChild.enum_type = value;
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
    /* Phase 6: `binds: TypeName` — this prefab is a view over an instance
     * of TypeName (model or cascade).  Runtime uses it to route child
     * bindings to the right model-instance per prefab-instance. */
    else if (currentKey == "binds") prefabNode->binds = value;

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
