#pragma once
#include <memory>
#include <string>
#include "../ast_nodes.h"
#include "../ast_builder.h"

namespace AIML {
namespace AST {

class Builder;

/// Parser for one α A β rule inside a function-cascade member class's `rules:`
/// list (spec/57).  A rule is a map:
///
///   - before: [revive, OwnerRef]      # ordered mask sequence (prefix TAIL)
///     on:     OwnerRef                 # a single Mask (scalar | selection | "*")
///     after:  [GameZone, CardType]     # ordered mask sequence (suffix HEAD); [] = terminal
///     weight: 5                        # optional
///     def:    { name: …, args: …, return: …, body: … }
///
/// A mask slot is a scalar (one token), the quoted "*" (wildcard), or a nested
/// list (a selection of tokens).  Tokens stay raw here; a later milestone resolves
/// them against the stage enum.
class RuleBuilder : public StateHandler {
public:
    RuleBuilder(Builder* b) : StateHandler(b), node_(std::make_unique<RuleDecl>()) {}
    bool OnMapStart() override;
    bool OnMapEnd() override;
    bool OnScalar(const std::string& value, const std::string& tag) override;
    bool OnSequenceStart() override;
    bool OnSequenceEnd() override;
    void receiveChildNode(std::unique_ptr<Node> node) override;
    std::unique_ptr<Node> finish() override;

private:
    enum class Seq { None, Before, After, On };

    std::unique_ptr<RuleDecl> node_;
    int  mapDepth = 0;
    bool expectingKey = true;
    std::string currentKey;
    Seq  seqCtx = Seq::None;
    int  slotDepth = 0;          // 1 = inside a nested selection slot
    Mask curSelection;           // accumulates a nested-list selection

    void pushSlotScalar(const std::string& value);
};

} // namespace AST
} // namespace AIML
