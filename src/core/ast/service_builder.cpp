#include "../clearance.h"
#include "service_builder.h"
#include "function_builder.h"
#include "../ast_builder.h"
#include "../../../logger.h"

namespace AIML {
namespace AST {

// Map depths (only maps count):
//   1 = the service body        { name:, base:, transport:, auth:, endpoints: }
//   2 = one endpoints element   { def: ... }   ("def" then delegates)
bool ServiceBuilder::OnMapStart() {
    ++depth;
    expectingKey = true;
    return true;
}

bool ServiceBuilder::OnMapEnd() {
    --depth;
    // An endpoint element closed; the next scalar at body level is a key again.
    expectingKey = true;
    if (depth == 0) builder->popState();   // service body closed
    return true;
}

bool ServiceBuilder::OnSequenceStart() {
    if (currentKey == "endpoints") inEndpoints = true;
    else if (currentKey == "transport" || currentKey == "transports") inTransport = true;
    return true;
}

bool ServiceBuilder::OnSequenceEnd() {
    if (inTransport) inTransport = false;
    else if (inEndpoints) inEndpoints = false;
    // A sequence was the VALUE of the current key — what follows is a key again.
    // Without this the next key is read as another value: `transport: [rpc]`
    // followed by `auth: none` swallowed "auth" into the transport list and the
    // service never saw its `endpoints:` (diagnosed 2026-10-18).
    expectingKey = true;
    return true;
}

bool ServiceBuilder::OnScalar(const std::string& value, const std::string& tag) {
    (void)tag;
    if (inTransport) { node_->transports.push_back(value); return true; }

    if (expectingKey) {
        currentKey = value;
        expectingKey = false;
        // An endpoint element is `- def: {...}` — hand the def to FunctionBuilder;
        // its `http`/`roles`/`cli` keys land on the FunctionDecl (spec/66 §2).
        if (inEndpoints && currentKey == "def") {
            builder->pushState(std::make_unique<FunctionBuilder>(builder));
            expectingKey = true;
        }
        return true;
    }

    expectingKey = true;
    if (currentKey == "name")      node_->name = value;
    else if (currentKey == "base") node_->base = value;
    else if (currentKey == "auth") node_->auth = value;
    else if (currentKey == "transport" || currentKey == "transports")
        node_->transports.push_back(value);   // scalar form: `transport: rpc`
    return true;
}

void ServiceBuilder::receiveChildNode(std::unique_ptr<Node> node) {
    if (auto* fn = dynamic_cast<FunctionDecl*>(node.get())) {
        node.release();
        ServiceEndpoint endpoint;
        endpoint.fn = std::unique_ptr<FunctionDecl>(fn);
        node_->endpoints.push_back(std::move(endpoint));
    }
    expectingKey = true;
}

std::unique_ptr<Node> ServiceBuilder::finish() {
    return std::move(node_);
}

} // namespace AST
} // namespace AIML
