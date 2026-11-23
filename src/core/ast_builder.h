#pragma once
#include <memory>
#include <string>
#include <vector>
#include <stack>
#include <yaml-cpp/yaml.h>
#include <yaml-cpp/eventhandler.h>
#include "ast_nodes.h"

namespace AIML {
namespace AST {

class Builder;

// spec/04 "quoting operator keys": `{ op: [args] }` is a YAML flow map, and
// some of the symbolic operators cannot stand bare there -- `*` `%` `>` `>=`
// `||` `?` throw, `&` `&&` `!` `~` do NOT throw and yield an empty or
// `null` key.  Returns the first bare reserved operator key in
// flow key position (`{ op:` or `, op:`), otherwise "".  Purely lexical, so only
// use it to EXPLAIN a failure that is already established -- the search also matches in data in
// quotation marks (`"{ *: [a, b] }"` as test input).
std::string bareOperatorKey(const std::string& text);

// The line the event parser is currently delivering (1-based, 0 = unknown).
// `parseInterpolatedString` only gets the string and otherwise has no
// location -- but in a 10,000-line module the location is half the
// message.  Thread-local, because several sources are compiled in parallel.
extern thread_local int g_builderLine;

std::unique_ptr<Expr> parseInterpolatedString(const std::string& input);
std::unique_ptr<Expr> parseScalarExpr(const std::string& val, const std::string& tag);
std::unique_ptr<Expr> nodeToExpr(const YAML::Node& node);

// File of the stream currently being parsed.  The free helpers of this unit
// (nodeToExpr & co.) have no builder, so they could not name a file and
// wrote "line 42: " as prose (spec/49 F49-100).  The compiler sets the
// name before parsing; thread-local, because imports parse recursively in the same thread
// and every run needs its own name.
void setCurrentParseFile(const std::string& file);
std::string currentParseFile();
// "file:line:column: " from the YAML mark, or "line N: " without a file.
std::string locOf(const YAML::Node& node);

// Base class for object-oriented states in the streaming parser
class StateHandler {
protected:
    Builder* builder;
public:
    StateHandler(Builder* b) : builder(b) {}
    virtual ~StateHandler() = default;

    // Return true if the child handled it, false if it's invalid
    virtual bool OnScalar(const std::string& value, const std::string& tag) { return false; }
    virtual bool OnSequenceStart() { return false; }
    virtual bool OnSequenceEnd() { return false; }
    virtual bool OnMapStart() { return false; }
    virtual bool OnMapEnd() { return false; }
    
    // Called by the Builder when a child state finishes and returns a built node
    virtual void receiveChildNode(std::unique_ptr<Node> node) {}
    
    // Called when this state handler is popped off the stack
    virtual std::unique_ptr<Node> finish() { return nullptr; }
};

class Builder : public YAML::EventHandler {
public:
    Builder();
    std::unique_ptr<Program> takeProgram();
    void addDeclaration(std::unique_ptr<Node> node);

    void pushState(std::unique_ptr<StateHandler> state);
    void popState();

    // Current 1-based line in the AIML source (updated on every YAML event).
    // State handlers can read this to record source locations on AST nodes.
    int currentLine = 0;
    // Column as well, 1-based.  The YAML mark always carried it; without it a
    // tool can only map a finding to the LINE, not to the token
    // (spec/49 F49-100).
    int currentColumn = 0;

    // Source file being parsed right now.  Until now the parser ran without a
    // file name, so a message from a builder could only name the text,
    // not the location (measured on the list default check: the
    // author was faced with "field 'liste' has a non-empty list default" without
    // knowing in WHICH file).  Empty for synthesised snippets.
    std::string sourceFile;

    // "file:line:column: " for a message from a builder, or "" without a
    // file (spec/49 F49-100: the location is a prefix in EXACTLY one form --
    // never prose like "line 9: ", which no tool can read).
    std::string fmtLoc() const { return fmtLoc(currentLine, currentColumn); }

    // The same form for a site that captured its position earlier
    // (StatementBuilder::capturedLine/-Column): a builder often reports only when
    // closing the node, and by then currentLine has already moved on.
    std::string fmtLoc(int line, int col) const {
        if (sourceFile.empty()) return "";
        if (line <= 0) return sourceFile + ": ";
        std::string loc = sourceFile + ":" + std::to_string(line);
        if (col > 0) loc += ":" + std::to_string(col);
        return loc + ": ";
    }

    // YAML::EventHandler Overrides
    void OnDocumentStart(const YAML::Mark& mark) override {}
    void OnDocumentEnd() override {}
    void OnNull(const YAML::Mark& mark, YAML::anchor_t anchor) override;
    void OnAlias(const YAML::Mark& mark, YAML::anchor_t anchor) override {}
    void OnScalar(const YAML::Mark& mark, const std::string& tag, YAML::anchor_t anchor, const std::string& value) override;
    void OnSequenceStart(const YAML::Mark& mark, const std::string& tag, YAML::anchor_t anchor, YAML::EmitterStyle::value style) override;
    void OnSequenceEnd() override;
    void OnMapStart(const YAML::Mark& mark, const std::string& tag, YAML::anchor_t anchor, YAML::EmitterStyle::value style) override;
    void OnMapEnd() override;

private:
    std::unique_ptr<Program> program;
    std::stack<std::unique_ptr<StateHandler>> stateStack;
};


} // namespace AST
} // namespace AIML
