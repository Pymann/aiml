#pragma once
#include <memory>
#include <string>
#include <vector>
#include "../ast_nodes.h"

namespace AIML {
namespace AST {

class Builder;
class StateHandler;

/*
 * MaterialBuilder — parses a `- material:` top-level block.
 *
 * Expected YAML shape:
 *
 *   - material:
 *       name: CrystalBlue
 *       shader: "resources/shaders/crystal.frag"
 *       uniforms:
 *         u_intensity: 0.6              # → float
 *         u_tint: [0.4, 0.7, 1.0, 1.0]  # → vec4
 *         u_count: { int: 42 }          # → int
 *         u_noise: { tex: "noise.png" } # → sampler2D
 *
 * Uniform value forms:
 *   - bare scalar number → float
 *   - list of 2/3/4 numbers → vec2/vec3/vec4
 *   - map { int: N } → int
 *   - map { tex: "path" } → sampler2D
 */
class MaterialBuilder : public StateHandler {
    std::unique_ptr<MaterialDecl> matNode;

    int  mapDepth = 0;
    bool expectingKey = false;
    std::string currentKey;

    // Inside the `uniforms:` block:
    bool insideUniforms = false;
    bool uniformExpectingKey = false;
    std::string uniformKey;

    // Current uniform being parsed:
    MaterialUniformInfo currentUniform;
    bool uniformIsTagMap = false;   // currently parsing { int: N } / { tex: "..." }
    std::string tagMapKey;           // last inner key inside the tag-map

    // Sequence parsing (vec2/3/4):
    bool insideVecSeq = false;
    std::vector<float> currentVec;

public:
    MaterialBuilder(Builder* b) : StateHandler(b) { matNode = std::make_unique<MaterialDecl>(); }
    bool OnMapStart() override;
    bool OnMapEnd() override;
    bool OnScalar(const std::string& value, const std::string& tag) override;
    bool OnSequenceStart() override;
    bool OnSequenceEnd() override;
    void receiveChildNode(std::unique_ptr<Node> node) override;
    std::unique_ptr<Node> finish() override;
};

} // namespace AST
} // namespace AIML
