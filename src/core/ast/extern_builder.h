#pragma once
#include <memory>
#include <string>
#include "../ast_nodes.h"

namespace AIML {
namespace AST {

class Builder;

// Parses an `extern: "C"` top-level block:
//
//   - extern: "C"
//     defs:
//       - def:
//           name: SDL_Init
//           args: [[flags, i32]]
//           return: i32
//
class ExternBuilder : public StateHandler {
    enum class State {
        OUTER,            // top level: waiting for "defs" key or outer MapEnd
        IN_DEFS,          // inside the defs sequence
        IN_DEF_OUTER_MAP, // inside { def: { … } } outer wrapper
        SAW_DEF_KEY,      // just saw the "def" key, waiting for its map value
        IN_DEF_BODY,      // inside the def body {name:…, args:…, return:…}
        IN_ARGS,          // inside the args sequence
    };

    std::unique_ptr<ExternBlock> block;
    State state = State::OUTER;

    // OUTER state
    bool        outerExpectKey  = true;
    std::string currentOuterKey;

    // IN_DEF_OUTER_MAP state
    bool defOuterExpectKey = false;

    // IN_DEF_BODY state
    bool        defExpectKey  = false;
    std::string defCurrentKey;
    ExternFuncDecl currentFunc;

    // IN_ARGS state
    int         argPairDepth  = 0;
    std::string currentArgName;

    // Generic depth tracking
    int mapDepth  = 0;
    int skipDepth = 0;

public:
    ExternBuilder(Builder* b, const std::string& abi)
        : StateHandler(b), block(std::make_unique<ExternBlock>())
    {
        block->abi = abi;
    }

    bool OnMapStart() override;
    bool OnMapEnd()   override;
    bool OnSequenceStart() override;
    bool OnSequenceEnd()   override;
    bool OnScalar(const std::string& value, const std::string& tag) override;

    void receiveChildNode(std::unique_ptr<Node>) override {}
    std::unique_ptr<Node> finish() override { return std::move(block); }
};

} // namespace AST
} // namespace AIML
