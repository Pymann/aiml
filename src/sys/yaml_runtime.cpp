#include "yaml_runtime.h"
#include "string_runtime.h"

#include <yaml-cpp/yaml.h>

#include <memory>
#include <string>
#include <vector>

namespace {
enum YamlKind : int32_t { kYamlNull, kYamlScalar, kYamlSequence, kYamlMapping };
struct YamlDocument;
struct YamlNode { YamlDocument* document; YAML::Node value; };
struct YamlDocument {
    bool active = true;
    YAML::Node root;
    std::string error;
    std::vector<std::unique_ptr<YamlNode>> nodes;
};

static char* copyString(const std::string& value) {
    return aiml_str_ndup(value.c_str(), value.size());
}
static YamlDocument* asDocument(void* raw) { return static_cast<YamlDocument*>(raw); }
static YamlNode* asNode(void* raw) {
    auto* node = static_cast<YamlNode*>(raw);
    return node && node->document && node->document->active ? node : nullptr;
}
static void* makeNode(YamlDocument* document, const YAML::Node& value) {
    if (!document || !document->active || !value.IsDefined()) return nullptr;
    auto node = std::make_unique<YamlNode>();
    node->document = document;
    node->value = value;
    void* raw = node.get();
    document->nodes.push_back(std::move(node));
    return raw;
}
static int32_t kindOf(const YAML::Node& node) {
    if (!node.IsDefined() || node.IsNull()) return kYamlNull;
    if (node.IsScalar()) return kYamlScalar;
    if (node.IsSequence()) return kYamlSequence;
    return node.IsMap() ? kYamlMapping : kYamlNull;
}
static bool validIndex(int32_t index, size_t size) {
    return index >= 0 && static_cast<size_t>(index) < size;
}
static YAML::const_iterator entryAt(const YAML::Node& node, int32_t index) {
    auto entry = node.begin();
    for (int32_t current = 0; current < index; ++current) ++entry;
    return entry;
}
static YamlDocument* parseDocument(const char* source, bool fromFile) {
    auto* document = new YamlDocument();
    try {
        document->root = fromFile ? YAML::LoadFile(source ? source : "")
                                  : YAML::Load(source ? source : "");
    } catch (const YAML::Exception& error) {
        document->root = YAML::Node();
        document->error = error.what();
    } catch (const std::exception& error) {
        document->root = YAML::Node();
        document->error = error.what();
    }
    return document;
}
} // namespace

extern "C" void* aiml_yaml_parse(const char* source) { return parseDocument(source, false); }
extern "C" void* aiml_yaml_parse_file(const char* path) { return parseDocument(path, true); }
extern "C" char* aiml_yaml_error(void* raw) {
    auto* document = asDocument(raw);
    return copyString(document && document->active ? document->error : "");
}
extern "C" void aiml_yaml_free(void* raw) {
    auto* document = asDocument(raw);
    if (!document || !document->active) return;
    document->active = false;
    document->root = YAML::Node();
    // Keep node handle tombstones alive so post-free calls cannot dereference freed memory.
}
extern "C" void* aiml_yaml_root(void* raw) {
    auto* document = asDocument(raw);
    return document && document->active && document->error.empty()
        ? makeNode(document, document->root) : nullptr;
}
extern "C" int32_t aiml_yaml_kind(void* raw) {
    auto* node = asNode(raw); return node ? kindOf(node->value) : kYamlNull;
}
extern "C" int32_t aiml_yaml_size(void* raw) {
    auto* node = asNode(raw);
    return node && (node->value.IsSequence() || node->value.IsMap())
        ? static_cast<int32_t>(node->value.size()) : 0;
}
extern "C" char* aiml_yaml_scalar(void* raw) {
    auto* node = asNode(raw);
    return copyString(node && node->value.IsScalar() ? node->value.Scalar() : "");
}
extern "C" int32_t aiml_yaml_line(void* raw) {
    auto* node = asNode(raw); if (!node) return 0;
    YAML::Mark mark = node->value.Mark(); return mark.is_null() ? 0 : mark.line + 1;
}
extern "C" int32_t aiml_yaml_column(void* raw) {
    auto* node = asNode(raw); if (!node) return 0;
    YAML::Mark mark = node->value.Mark(); return mark.is_null() ? 0 : mark.column + 1;
}
extern "C" char* aiml_yaml_dump(void* raw) {
    auto* node = asNode(raw); if (!node) return copyString("");
    try { return copyString(YAML::Dump(node->value)); }
    catch (const YAML::Exception&) { return copyString(""); }
}
extern "C" void* aiml_yaml_item(void* raw, int32_t index) {
    auto* node = asNode(raw);
    if (!node || !node->value.IsSequence() || !validIndex(index, node->value.size())) return nullptr;
    return makeNode(node->document, node->value[static_cast<size_t>(index)]);
}
extern "C" char* aiml_yaml_key(void* raw, int32_t index) {
    auto* node = asNode(raw);
    if (!node || !node->value.IsMap() || !validIndex(index, node->value.size())) return copyString("");
    auto entry = entryAt(node->value, index);
    return entry->first.IsScalar() ? copyString(entry->first.Scalar()) : copyString("");
}
extern "C" void* aiml_yaml_value(void* raw, int32_t index) {
    auto* node = asNode(raw);
    if (!node || !node->value.IsMap() || !validIndex(index, node->value.size())) return nullptr;
    return makeNode(node->document, entryAt(node->value, index)->second);
}
extern "C" void* aiml_yaml_get(void* raw, const char* key) {
    auto* node = asNode(raw); if (!node || !node->value.IsMap() || !key) return nullptr;
    for (auto entry : node->value) {
        if (entry.first.IsScalar() && entry.first.Scalar() == key)
            return makeNode(node->document, entry.second);
    }
    return nullptr;
}
