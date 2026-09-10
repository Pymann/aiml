// spec/61 §4 — conservative intraprocedural ownership analyzer (Increment B).
// Two passes: (1) assign stable ids/resources to every decl ONCE (so the loop fixpoint does
// not churn ids and always terminates, §4.3); (2) a forward flow analysis over blocks/if/while
// that marks a slot Owned only while it stays a fresh, un-rebound heap local. See the header.
#include "ownership_analyzer.h"
#include "ast_nodes.h"
#include "context.h"
#include <functional>

namespace AIML {
namespace AST {

namespace {

bool endsWith(const std::string& s, const char* suf) {
    std::string t(suf);
    return s.size() >= t.size() && s.compare(s.size() - t.size(), t.size(), t) == 0;
}

struct Analyzer {
    const CompilerContext& ctx;
    OwnershipPlan plan;
    std::uint32_t nextId = 1;
    std::uint32_t nextRes = 1;
    std::map<OwnershipId, ResourceId> ownedResource;   // fresh-owned decl id → its singleton resource
    std::map<const DeclStmt*, bool> declFreshOwned;    // pass-1 classification cache
    // lexical name → id stack (pass 2)
    std::vector<std::map<std::string, OwnershipId>> scopes;

    explicit Analyzer(const CompilerContext& c) : ctx(c) {}

    // A decl is a fresh, drop-registered owned heap local iff it matches EXACTLY the cases
    // visitDeclStmt registers a ScopeDrop for (list no-init / string-interp / model no-init /
    // model get_copy). Keeping this in lockstep with the drop registration is what makes the
    // gate correct — anything else stays non-owned and is never newly dropped.
    bool freshOwned(const DeclStmt* d) const {
        if (d->lifetime == "persistent" || d->is_extern) return false;
        const std::string& t = d->typeName;
        if (t.rfind("list<", 0) == 0 && !t.empty() && t.back() == '>' && !d->initialValue)
            return true;
        if (t == "string" && d->initialValue &&
            dynamic_cast<StringInterpolationExpr*>(d->initialValue.get()))
            return true;
        auto ci = ctx.classes.find(t);
        if (ci != ctx.classes.end() && ci->second.hasHeapFields && ci->second.serNodesVar) {
            if (!d->initialValue) return true;
            if (auto* c = dynamic_cast<CallExpr*>(d->initialValue.get()))
                if (endsWith(c->functionName, ".get_copy")) return true;
        }
        return false;
    }

    // ── Pass 1: assign ids + resources to all decls, recursively ────────────
    void assignIds(const BlockStmt* block) {
        if (!block) return;
        for (const auto& s : block->statements) assignIdsStmt(s.get());
    }
    void assignIdsStmt(const Stmt* s) {
        if (auto* d = dynamic_cast<const DeclStmt*>(s)) {
            OwnershipId id = nextId++;
            plan.declarations[d] = id;
            bool fresh = freshOwned(d);
            declFreshOwned[d] = fresh;
            if (fresh) ownedResource[id] = nextRes++;
        } else if (auto* i = dynamic_cast<const IfStmt*>(s)) {
            assignIds(i->thenBranch.get());
            assignIds(i->elseBranch.get());
        } else if (auto* w = dynamic_cast<const WhileStmt*>(s)) {
            assignIds(w->body.get());
        } else if (auto* b = dynamic_cast<const BlockStmt*>(s)) {
            assignIds(b);
        }
    }

    // ── lexical helpers (pass 2) ─────────────────────────────────────────────
    OwnershipId lookup(const std::string& name) const {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
            auto f = it->find(name);
            if (f != it->end()) return f->second;
        }
        return NoOwnership;
    }

    // target string → plain local name, or "" if it is a member/index/deref target
    static std::string plainLocalTarget(const std::string& target) {
        if (target.find('.') != std::string::npos) return "";
        if (target.find('-') != std::string::npos) return "";
        if (target.find('<') != std::string::npos) return "";
        return target;
    }

    // ── Pass 2: forward flow ─────────────────────────────────────────────────
    FlowState analyzeBlock(const BlockStmt* block, FlowState in) {
        if (!block) return in;
        scopes.emplace_back();
        std::vector<OwnershipId> blockLocals;
        FlowState st = std::move(in);
        for (const auto& s : block->statements)
            st = analyzeStmt(s.get(), std::move(st), blockLocals);
        plan.normalExits[block] = st;                    // BEFORE removing block-locals
        for (OwnershipId id : blockLocals) st.slots.erase(id);
        scopes.pop_back();
        return st;
    }

    FlowState analyzeStmt(const Stmt* s, FlowState st, std::vector<OwnershipId>& blockLocals) {
        if (!st.reachable) return st;
        if (auto* d = dynamic_cast<const DeclStmt*>(s)) {
            OwnershipId id = plan.declarations.at(d);
            scopes.back()[d->varName] = id;
            blockLocals.push_back(id);
            OwnFact f;
            f.life = Live;
            if (declFreshOwned.at(d)) {
                f.origin = Origin::Owned;
                f.resources.insert(ownedResource.at(id));
            } else {
                f.origin = Origin::Borrowed;   // no resource → never newly dropped
            }
            st.slots[id] = std::move(f);
            return st;
        }
        if (auto* set = dynamic_cast<const SetStmt*>(s)) {
            // spec/61 §7.0 (Increment C): for a MEMBER store, record whether the source is an
            // owned local (→ Move, shallow, current) or anything else — a param/view/rebound
            // local (→ DeepCopy, so the member gets an INDEPENDENT heap and the aggregate cannot
            // later double-free a shared sub-heap). Codegen reads plan.statements[set].transfer.
            {
                const std::string& t = set->target;
                bool memberTgt = (t.rfind("<.", 0) == 0) || (t.find('.') != std::string::npos);
                bool dashIdx   = (t.find('-') != std::string::npos) && (t.find('.') == std::string::npos);
                if (memberTgt && !dashIdx) {
                    Origin srcO = Origin::Unknown;
                    if (auto* v = dynamic_cast<const VariableExpr*>(set->value.get()))
                        if (v->name.find('.') == std::string::npos && v->name.find('<') == std::string::npos) {
                            auto it = st.slots.find(lookup(v->name));
                            if (it != st.slots.end()) srcO = it->second.origin;
                        }
                    StatementPlan sp;
                    sp.transfer = (srcO == Origin::Owned) ? Transfer::Move : Transfer::DeepCopy;
                    plan.statements[set] = sp;
                }
            }
            std::string tgt = set->target;
            if (tgt.rfind("<.", 0) == 0) tgt.clear();     // member store → not a plain-local rebind
            std::string local = plainLocalTarget(tgt);
            if (!local.empty()) {
                OwnershipId id = lookup(local);
                if (id != NoOwnership) {
                    // self-assignment keeps the slot; any other rebind conservatively drops
                    // ownership (the RHS may be a shared view of another owner's heap → the
                    // slot's registered drop must NOT fire, else double-free).
                    bool selfAssign = false;
                    if (auto* v = dynamic_cast<const VariableExpr*>(set->value.get()))
                        selfAssign = (v->name == local);
                    if (!selfAssign) {
                        auto it = st.slots.find(id);
                        if (it != st.slots.end() && it->second.origin == Origin::Owned)
                            it->second.origin = Origin::Unknown;   // rebound → not provably owned
                    }
                }
            }
            return st;
        }
        if (auto* iff = dynamic_cast<const IfStmt*>(s)) {
            FlowState left  = iff->thenBranch ? analyzeBlock(iff->thenBranch.get(), st) : st;
            FlowState right = iff->elseBranch ? analyzeBlock(iff->elseBranch.get(), st) : st;
            return joinStates(left, right);
        }
        if (auto* wh = dynamic_cast<const WhileStmt*>(s)) {
            FlowState head = st;
            for (int guard = 0; guard < 10000; ++guard) {
                FlowState back = analyzeBlock(wh->body.get(), head);
                FlowState next = joinStates(st, back);
                if (sameState(next, head)) return next;
                head = std::move(next);
            }
            plan.suppressFunctionDrops = true;   // did not converge → suppress all new drops
            return head;
        }
        if (auto* b = dynamic_cast<const BlockStmt*>(s)) {
            return analyzeBlock(b, std::move(st));
        }
        if (dynamic_cast<const RetStmt*>(s)) {
            plan.earlyExits[s] = st;
            st.reachable = false;   // statements after a return on this path are dead
            return st;
        }
        // ExprStmt / calls / everything else: no ownership rebinding of a tracked slot in this
        // conservative cut (moves via push/return are handled by consumeLocalDrop at lowering).
        return st;
    }

    OwnershipPlan run(const FunctionDecl& fn) {
        // Parameters: borrows (no resource → never dropped). Recorded in the entry state so a
        // member-store from a param classifies as DeepCopy (Increment C).
        scopes.emplace_back();
        std::vector<OwnershipId> paramIds;
        for (size_t i = 0; i < fn.args.size(); ++i) {
            OwnershipId id = nextId++;
            scopes.back()[fn.args[i].first] = id;
            paramIds.push_back(id);
        }
        assignIds(fn.body.get());
        FlowState entry;
        entry.reachable = true;
        for (OwnershipId id : paramIds) entry.slots[id] = OwnFact{Origin::Borrowed, Live, {}};
        analyzeBlock(fn.body.get(), std::move(entry));
        scopes.pop_back();
        return std::move(plan);
    }
};

} // namespace

OwnershipPlan buildOwnershipPlan(const CompilerContext& ctx, const FunctionDecl& fn) {
    Analyzer a(ctx);
    return a.run(fn);
}

} // namespace AST
} // namespace AIML
