#ifndef AIML_YAML_DUMP_H
#define AIML_YAML_DUMP_H

// Restore source text for a bundle WITHOUT losing the quoting.
//
// In aiml quoting is meaning, not formatting: `nodeToExpr`
// (src/core/ast_builder.cpp) tells a string literal from a name
// exactly by the yaml-cpp tag "!", which only quoted scalars carry.  `YAML::Dump`
// throws this tag away — `call: [parts.push, "eins"]` becomes
// `call: [parts.push, eins]`, and at the receiver "eins" is suddenly a
// name.  Measured: the body reported `Unknown Type: 'eins'`.
//
// Hence an emitter here that leaves quoted scalars quoted.  It is the
// only permitted way to produce bundle source text (spec/29_2 §19).
#include <string>
#include <yaml-cpp/yaml.h>

namespace aiml {

inline void emitPreservingQuotes(YAML::Emitter& out, const YAML::Node& node) {
    switch (node.Type()) {
        case YAML::NodeType::Scalar:
            if (node.Tag() == "!") out << YAML::DoubleQuoted << node.Scalar();
            else                   out << node.Scalar();
            break;
        case YAML::NodeType::Sequence:
            // The STYLE is source text too: `decl: [got, i32]` in flow style
            // becomes three lines in block style.  yaml-cpp remembers the style
            // on the node; whoever discards it sends a different file than
            // the one the author wrote (measured: the socket test
            // failed because of that, not because of the quoting).
            if (node.Style() == YAML::EmitterStyle::Flow) out << YAML::Flow;
            out << YAML::BeginSeq;
            for (const auto& item : node) emitPreservingQuotes(out, item);
            out << YAML::EndSeq;
            break;
        case YAML::NodeType::Map:
            if (node.Style() == YAML::EmitterStyle::Flow) out << YAML::Flow;
            out << YAML::BeginMap;
            for (auto it = node.begin(); it != node.end(); ++it) {
                out << YAML::Key;
                emitPreservingQuotes(out, it->first);
                out << YAML::Value;
                emitPreservingQuotes(out, it->second);
            }
            out << YAML::EndMap;
            break;
        case YAML::NodeType::Null:
            out << YAML::Null;
            break;
        default:
            out << "";
            break;
    }
}

inline std::string dumpPreservingQuotes(const YAML::Node& node) {
    YAML::Emitter out;
    emitPreservingQuotes(out, node);
    const char* s = out.c_str();
    return s ? std::string(s) : std::string();
}

} // namespace aiml
#endif
