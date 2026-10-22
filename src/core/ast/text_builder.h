#pragma once
#include "../ast_builder.h"
#include "../ast_nodes.h"

namespace AIML {
namespace AST {

// Handles: - text: <varname>
//            value: |
//              multiline content...
// Produces a DeclStmt(varname, "string", LiteralExpr(content, "string")).
class TextBuilder : public StateHandler {
    std::string varName;
    std::string content;
    int scalarCount = 0; // 0=varname, 1="value" key (ignored), 2=content
public:
    TextBuilder(Builder* b) : StateHandler(b) {}
    bool OnScalar(const std::string& value, const std::string&) override {
        if      (scalarCount == 0) varName = value;
        else if (scalarCount == 2) content = value;
        ++scalarCount;
        return true;
    }
    bool OnMapEnd() override { builder->popState(); return true; }
    std::unique_ptr<Node> finish() override {
        auto decl = std::make_unique<DeclStmt>(varName, "string");
        decl->initialValue = std::make_unique<LiteralExpr>(content, "string");
        // Ein Textblock ist ein BENANNTES LITERAL: es gibt keine Syntax, ihm
        // etwas zuzuweisen.  Ohne `const` meldete der Globals-Lint (spec/21
        // §6.2) jeden Shader-Quelltext als herrenlosen Zustand — und bestrafte
        // damit genau das Benennen, das er belohnen will.
        decl->is_const = true;
        return decl;
    }
};

} // namespace AST
} // namespace AIML
