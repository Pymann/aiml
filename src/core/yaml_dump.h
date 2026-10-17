#ifndef AIML_YAML_DUMP_H
#define AIML_YAML_DUMP_H

// Quelltext fuer ein Bundle wiederherstellen, OHNE die Zitierung zu verlieren.
//
// In aiml ist die Zitierung Bedeutung, nicht Formatierung: `nodeToExpr`
// (src/core/ast_builder.cpp) unterscheidet ein String-Literal von einem Namen
// genau am yaml-cpp-Tag "!", das nur zitierte Skalare tragen.  `YAML::Dump`
// wirft dieses Tag weg — aus `call: [parts.push, "eins"]` wird
// `call: [parts.push, eins]`, und beim Empfaenger ist "eins" plotzlich ein
// Name.  Gemessen: der Rumpf meldete `Unknown Type: 'eins'`.
//
// Darum hier ein Emitter, der zitierte Skalare zitiert laesst.  Er ist der
// einzige erlaubte Weg, Bundle-Quelltext zu erzeugen (spec/29_2 §19).
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
            // Auch der STIL ist Quelltext: `decl: [got, i32]` im Fluss-Stil
            // wird im Block-Stil zu drei Zeilen.  yaml-cpp merkt sich den Stil
            // am Knoten; wer ihn verwirft, verschickt eine andere Datei als
            // die, die der Autor geschrieben hat (gemessen: der Socket-Test
            // scheiterte daran, nicht an der Zitierung).
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
