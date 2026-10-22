#pragma once
#include <memory>
#include <string>
#include "../ast_nodes.h"
#include "../ast_builder.h"

namespace AIML {
namespace AST {

class Builder;

/// Parser for top-level `- service:` blocks (spec/66 — one declaration, every
/// transport).
///
///   - service:
///       name: LioApi
///       base: "/api/v1"
///       transport: [rpc, rest]
///       auth: TokenAuth
///       endpoints:
///         - def:
///             name: resolve_component
///             http: [post, "/components/resolve"]
///             roles: [viewer]
///             args: [[req, ResolveReq]]
///             return: ResolveResp
///             body: [ ... ]
///
/// The endpoint annotations (`http`, `roles`, `cli`) are parsed by FunctionBuilder
/// onto the FunctionDecl itself, so an endpoint reads as one `def`.  Synthesis and
/// the static checks live in the compiler pass (spec/66 §3, §5).
class ServiceBuilder : public StateHandler {
public:
    ServiceBuilder(Builder* b)
        : StateHandler(b), node_(std::make_unique<ServiceDecl>()) {}
    bool OnMapStart() override;
    bool OnMapEnd() override;
    bool OnScalar(const std::string& value, const std::string& tag) override;
    bool OnSequenceStart() override;
    bool OnSequenceEnd() override;
    void receiveChildNode(std::unique_ptr<Node> node) override;
    std::unique_ptr<Node> finish() override;

private:
    std::unique_ptr<ServiceDecl> node_;
    int  depth = 0;              // map depth: 1 = service body, 2 = an endpoint element
    bool expectingKey = true;
    std::string currentKey;
    bool inEndpoints = false;    // inside the `endpoints:` sequence
    bool inTransport = false;    // inside the `transport:` list
};

} // namespace AST
} // namespace AIML
