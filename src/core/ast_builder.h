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

std::unique_ptr<Expr> parseInterpolatedString(const std::string& input);
std::unique_ptr<Expr> nodeToExpr(const YAML::Node& node);

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

    // YAML::EventHandler Overrides
    void OnDocumentStart(const YAML::Mark& mark) override {}
    void OnDocumentEnd() override {}
    void OnNull(const YAML::Mark& mark, YAML::anchor_t anchor) override {}
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
