#pragma once
// groupinterface_builder.h — `- groupinterface:` (spec/58 §12).
//
//   - groupinterface:
//       name: LayerStore
//       exports: [[registry, Registry], [codec, Codec]]
//
// Parses straight into an InterfaceDecl (groupInterface = true) whose methods
// are the export accessors `__gi_<inst>` returning `ref<Iface>` — the lowering
// of §12.3 happens here, so everything downstream (registration, vtables,
// upcasts, cross-unit headers) is the ordinary interface path.
#include <memory>
#include <string>
#include "../ast_nodes.h"
#include "../ast_builder.h"

namespace AIML {
namespace AST {

class GroupInterfaceBuilder : public StateHandler {
public:
    GroupInterfaceBuilder(Builder* b) : StateHandler(b), node_(std::make_unique<InterfaceDecl>()) {
        node_->groupInterface = true;
    }
    bool OnMapStart() override;
    bool OnMapEnd() override;
    bool OnScalar(const std::string& value, const std::string& tag) override;
    bool OnSequenceStart() override;
    bool OnSequenceEnd() override;
    std::unique_ptr<Node> finish() override;

private:
    std::unique_ptr<InterfaceDecl> node_;
    std::string currentKey;
    bool expectingKey = true;
    int  exportsDepth = 0;   // 1 = the exports list, 2 = one [inst, Iface] pair
    int  field = 0;
    std::pair<std::string, std::string> cur;
};

}  // namespace AST
}  // namespace AIML
