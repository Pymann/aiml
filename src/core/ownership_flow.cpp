// spec/61 §4 — ownership dataflow: pure merge/compare kernel + drop-permission check.
// LLVM-free. See ownership_flow.h and the implementation packet.
#include "ownership_flow.h"

namespace AIML {
namespace AST {

// Join two facts for the SAME slot arriving on two control-flow edges. Origin agrees or
// becomes Unknown; life bits union (so live-on-one-edge + moved-on-other = Live|Moved, which
// the diagnostics later flag); resources union (a merge must never lose an alias).
OwnFact joinFact(const OwnFact& a, const OwnFact& b) {
    OwnFact out;
    out.origin = (a.origin == b.origin) ? a.origin : Origin::Unknown;
    out.life = static_cast<std::uint8_t>(a.life | b.life);
    out.resources = a.resources;
    out.resources.insert(b.resources.begin(), b.resources.end());
    return out;
}

// Join two control-flow states at a merge point. An unreachable predecessor contributes
// nothing. A slot present on only one side is joined against a default (Uninit) fact so its
// liveness/origin degrade conservatively.
FlowState joinStates(const FlowState& a, const FlowState& b) {
    if (!a.reachable) return b;
    if (!b.reachable) return a;
    FlowState out = a;
    for (const auto& [id, rhs] : b.slots) {
        auto it = out.slots.find(id);
        if (it == out.slots.end())
            out.slots.emplace(id, joinFact(OwnFact{}, rhs));
        else
            it->second = joinFact(it->second, rhs);
    }
    for (const auto& [id, lhs] : a.slots) {
        if (b.slots.find(id) == b.slots.end())
            out.slots[id] = joinFact(lhs, OwnFact{});
    }
    return out;
}

// Fixpoint termination test: identical reachability + identical per-slot facts.
bool sameState(const FlowState& a, const FlowState& b) {
    if (a.reachable != b.reachable || a.slots.size() != b.slots.size())
        return false;
    for (const auto& [id, x] : a.slots) {
        auto it = b.slots.find(id);
        if (it == b.slots.end()) return false;
        const OwnFact& y = it->second;
        if (x.origin != y.origin || x.life != y.life || x.resources != y.resources)
            return false;
    }
    return true;
}

bool mayDrop(const OwnershipPlan& plan, const FlowState& exit, OwnershipId id) {
    if (!exit.reachable || plan.suppressFunctionDrops || id == NoOwnership)
        return false;
    auto it = exit.slots.find(id);
    if (it == exit.slots.end()) return false;
    const OwnFact& f = it->second;
    // Exactly Owned + exactly Live (not merged with Moved/Uninit) + owns real resources.
    if (f.origin != Origin::Owned || f.life != Live || f.resources.empty())
        return false;
    for (ResourceId r : f.resources)
        if (plan.suppressNewDrops.count(r)) return false;
    return true;
}

} // namespace AST
} // namespace AIML
