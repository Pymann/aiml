#pragma once
// spec/61 §4 — intraprocedural ownership dataflow (Increment A: pure data model + merges).
// This module is LLVM-free: it computes an OwnershipPlan that the codegen later consults to
// decide whether a scope-drop is provably safe. Until it is wired in, it is dead code with
// zero effect on emission. Design: doc/plans/ownership_dataflow_implementation_packet_2026-09-10.md
#include <cstdint>
#include <map>
#include <set>
#include <vector>

namespace AIML {
namespace AST {

// AST nodes (defined in ast_nodes.h) — only used as opaque keys here.
class Expr;
class Stmt;
class DeclStmt;
class BlockStmt;

using OwnershipId = std::uint32_t;   // one per declaration/parameter, stable across the fixpoint
using ResourceId  = std::uint32_t;   // one per allocation/copy site (heap identity)
constexpr OwnershipId NoOwnership = 0;

// Owner qualification of a slot's value. Orthogonal to path-liveness (LifeBits) — collapsing
// them into one enum would lose information at branch merges.
enum class Origin : std::uint8_t {
    None,        // scalar — no heap, never dropped
    Owned,       // this slot owns its heap and is responsible for freeing it
    Borrowed,    // a borrow (parameter, view) — never freed by this slot
    SharedView,  // legacy shallow-sharing of another owner's heap — NOT refcounting, NOT shared<T>
    Static,      // string literal / static data — never freed
    Unknown      // could not be classified — treated conservatively (no new drop)
};

// Path-liveness as a bitset so a merge can represent "live on one edge, moved on another".
enum LifeBits : std::uint8_t {
    Uninit = 1,
    Live   = 2,
    Moved  = 4
};

struct OwnFact {
    Origin origin = Origin::Unknown;
    std::uint8_t life = Uninit;
    std::set<ResourceId> resources;   // heap resources this slot's value refers to (for alias detection)
};

struct FlowState {
    bool reachable = true;
    std::map<OwnershipId, OwnFact> slots;
};

enum class Transfer : std::uint8_t {
    None,          // no ownership effect (scalar / side-effect-only)
    SelfAssign,    // set:[x,x] — no-op
    DeepCopy,      // independent copy into the target
    Move,          // ownership moved into a longer-lived sink (member/container/return/consumed)
    BindView,      // target binds a shallow view of another owner's heap — must NOT free it
    LegacyUnknown  // fall back to the pre-existing lowering; no new drop claimed
};

struct StatementPlan {
    Transfer transfer = Transfer::None;
    OwnershipId source = NoOwnership;
    OwnershipId target = NoOwnership;
    OwnFact result;
};

struct OwnershipPlan {
    std::map<const DeclStmt*, OwnershipId> declarations;
    std::map<const Stmt*, StatementPlan>   statements;
    std::map<const Expr*, OwnFact>         expressions;
    std::map<const BlockStmt*, FlowState>  normalExits;   // state BEFORE dropping block-locals
    std::map<const Stmt*, FlowState>       earlyExits;    // e.g. return-statement exit state
    std::set<ResourceId> suppressNewDrops;                // resources a warning has made unsafe to free
    bool suppressFunctionDrops = false;                   // unresolved alias/retain → no new drops at all
};

// ── pure merge / compare kernel (Increment A) ────────────────────────────────
OwnFact   joinFact(const OwnFact& a, const OwnFact& b);
FlowState joinStates(const FlowState& a, const FlowState& b);
bool      sameState(const FlowState& a, const FlowState& b);

// ── shared drop-permission check (Increment E; defined now, used when wired) ──
// A scope-drop is permitted ONLY for a provably-owned, live slot whose resources are not
// suppressed. Any uncertainty (no plan, unreachable, Unknown origin, empty resources,
// suppressed resource) → false = do NOT emit a new drop (leak, never a double-free).
bool mayDrop(const OwnershipPlan& plan, const FlowState& exit, OwnershipId id);

} // namespace AST
} // namespace AIML
