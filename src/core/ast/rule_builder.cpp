#include "rule_builder.h"
#include "../ast_builder.h"
#include "function_builder.h"
#include "../../../logger.h"
#include <stdexcept>

namespace AIML {
namespace AST {

void RuleBuilder::pushSlotScalar(const std::string& value) {
    // A bare slot in a before/after mask sequence.
    Mask m;
    if (value == "*") m.is_wildcard = true;
    else              m.options.push_back(value);
    if (seqCtx == Seq::Before) node_->before.push_back(std::move(m));
    else                       node_->after.push_back(std::move(m));
}

bool RuleBuilder::OnMapStart() {
    ++mapDepth;          // the rule map opens (nested def-map goes to FunctionBuilder)
    expectingKey = true;
    return true;
}

bool RuleBuilder::OnMapEnd() {
    --mapDepth;
    if (mapDepth == 0) builder->popState();
    return true;
}

bool RuleBuilder::OnScalar(const std::string& value, const std::string& /*tag*/) {
    // Inside a before/after mask sequence.
    if (seqCtx == Seq::Before || seqCtx == Seq::After) {
        if (slotDepth == 1) curSelection.options.push_back(value);  // nested selection
        else                pushSlotScalar(value);
        return true;
    }
    // Inside an `on:` selection sequence.
    if (seqCtx == Seq::On) {
        if (value == "*") node_->on.is_wildcard = true;
        else              node_->on.options.push_back(value);
        return true;
    }
    // Map level.
    if (expectingKey) {
        currentKey = value;
        expectingKey = false;
        if (currentKey == "def")
            builder->pushState(std::make_unique<FunctionBuilder>(builder));
        return true;
    }
    // Scalar value for a scalar-valued key.
    if (currentKey == "on") {
        if (value == "*") node_->on.is_wildcard = true;
        else              node_->on.options.push_back(value);
    } else if (currentKey == "weight") {
        try { node_->weight = std::stoi(value); node_->hasWeight = true; } catch (...) {}
    }
    expectingKey = true;
    return true;
}

bool RuleBuilder::OnSequenceStart() {
    if (seqCtx == Seq::Before || seqCtx == Seq::After) {
        // Nested selection slot begins.
        slotDepth = 1;
        curSelection = Mask{};
        return true;
    }
    if      (currentKey == "before") seqCtx = Seq::Before;
    else if (currentKey == "after")  { seqCtx = Seq::After; node_->hasAfter = true; }
    else if (currentKey == "on")     seqCtx = Seq::On;
    return true;
}

bool RuleBuilder::OnSequenceEnd() {
    if (seqCtx == Seq::Before || seqCtx == Seq::After) {
        if (slotDepth == 1) {                 // finish a selection slot
            if (seqCtx == Seq::Before) node_->before.push_back(std::move(curSelection));
            else                       node_->after.push_back(std::move(curSelection));
            curSelection = Mask{};
            slotDepth = 0;
        } else {                              // finish the mask sequence
            seqCtx = Seq::None;
            expectingKey = true;
        }
        return true;
    }
    if (seqCtx == Seq::On) {
        seqCtx = Seq::None;
        expectingKey = true;
    }
    return true;
}

void RuleBuilder::receiveChildNode(std::unique_ptr<Node> node) {
    if (dynamic_cast<FunctionDecl*>(node.get())) {
        node_->def = std::unique_ptr<FunctionDecl>(static_cast<FunctionDecl*>(node.release()));
        expectingKey = true;
    }
}

std::unique_ptr<Node> RuleBuilder::finish() {
    return std::move(node_);
}

} // namespace AST
} // namespace AIML
