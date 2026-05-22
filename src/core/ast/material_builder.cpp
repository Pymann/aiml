#include "../../../logger.h"
#include "../ast_builder.h"
#include "material_builder.h"
#include <cstdlib>

namespace AIML {
namespace AST {

bool MaterialBuilder::OnMapStart() {
    mapDepth++;
    if (insideUniforms) {
        if (uniformExpectingKey == false && !uniformKey.empty()) {
            /* We expect either a vec-list (handled in OnSequenceStart) or a
             * tag-map { int: N } / { tex: "..." } here. */
            uniformIsTagMap   = true;
            currentUniform    = MaterialUniformInfo();
            tagMapKey.clear();
            return true;
        }
        uniformExpectingKey = true;
        return true;
    }
    /* Detect entry into the `uniforms:` sub-map and switch mode. */
    if (currentKey == "uniforms") {
        insideUniforms = true;
        uniformExpectingKey = true;
        uniformKey.clear();
        currentKey.clear();
        return true;
    }
    if (mapDepth == 1) {
        expectingKey = true;
    }
    return true;
}

bool MaterialBuilder::OnMapEnd() {
    mapDepth--;
    if (insideUniforms) {
        if (uniformIsTagMap) {
            /* Finished one { int:N } / { tex:"..." } entry. Save it. */
            matNode->uniforms.emplace_back(uniformKey, currentUniform);
            uniformIsTagMap = false;
            uniformKey.clear();
            uniformExpectingKey = true;  /* next uniform key */
            return true;
        }
        /* End of the entire `uniforms:` map */
        insideUniforms = false;
        expectingKey = true;
        return true;
    }
    if (mapDepth <= 0) {
        builder->popState();
    }
    return true;
}

bool MaterialBuilder::OnScalar(const std::string& value, const std::string& tag) {
    /* Inside a vec-list (e.g. `u_tint: [0.4, 0.7, 1.0, 1.0]`), each element
     * arrives as a scalar here. Collect them until OnSequenceEnd. */
    if (insideVecSeq) {
        currentVec.push_back(strtof(value.c_str(), nullptr));
        return true;
    }
    if (insideUniforms) {
        if (uniformIsTagMap) {
            if (tagMapKey.empty()) {
                tagMapKey = value;
                return true;
            }
            /* tag-map value */
            if (tagMapKey == "int") {
                currentUniform.kind    = MaterialUniformInfo::K_INT;
                currentUniform.integer = (int32_t)strtol(value.c_str(), nullptr, 10);
            } else if (tagMapKey == "tex") {
                currentUniform.kind     = MaterialUniformInfo::K_TEX;
                currentUniform.tex_path = value;
            } else if (tagMapKey == "float" || tagMapKey == "f") {
                currentUniform.kind   = MaterialUniformInfo::K_FLOAT;
                currentUniform.scalar = strtof(value.c_str(), nullptr);
            }
            tagMapKey.clear();
            return true;
        }
        if (uniformExpectingKey) {
            uniformKey = value;
            uniformExpectingKey = false;
            return true;
        }
        /* Bare scalar → float */
        MaterialUniformInfo u;
        u.kind   = MaterialUniformInfo::K_FLOAT;
        u.scalar = strtof(value.c_str(), nullptr);
        matNode->uniforms.emplace_back(uniformKey, u);
        uniformKey.clear();
        uniformExpectingKey = true;
        return true;
    }

    if (expectingKey) {
        currentKey = value;
        expectingKey = false;
        return true;
    }

    if (currentKey == "name")   matNode->name   = value;
    else if (currentKey == "shader") matNode->shader = value;

    expectingKey = true;
    return true;
}

bool MaterialBuilder::OnSequenceStart() {
    if (insideUniforms && !uniformKey.empty() && !uniformExpectingKey) {
        /* A vec value — collect numbers until OnSequenceEnd. */
        insideVecSeq = true;
        currentVec.clear();
        return true;
    }
    return true;
}

bool MaterialBuilder::OnSequenceEnd() {
    if (insideVecSeq) {
        MaterialUniformInfo u;
        if (currentVec.size() == 2) u.kind = MaterialUniformInfo::K_VEC2;
        else if (currentVec.size() == 3) u.kind = MaterialUniformInfo::K_VEC3;
        else                              u.kind = MaterialUniformInfo::K_VEC4;
        u.vec = currentVec;
        matNode->uniforms.emplace_back(uniformKey, u);
        uniformKey.clear();
        currentVec.clear();
        insideVecSeq = false;
        uniformExpectingKey = true;
    }
    return true;
}

void MaterialBuilder::receiveChildNode(std::unique_ptr<Node>) { }
std::unique_ptr<Node> MaterialBuilder::finish() { return std::move(matNode); }

/* Helper: when we're inside the vec-sequence, the YAML library emits
 * scalars for each element. We intercept those by overriding OnScalar
 * for that path — but it shares the same OnScalar entry. Let's handle
 * the vec-element case via the same dispatcher by checking insideVecSeq
 * first. */

} // namespace AST
} // namespace AIML
