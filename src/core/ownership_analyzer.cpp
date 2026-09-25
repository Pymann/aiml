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

// spec/61 §7 KILL-SWITCH — MUST match kManagedStringLocals in ast_compiler_stmt.cpp. Disabled
// pending a fix for an open-match double-free; OFF → T1-era string ownership.
static constexpr bool kManagedStringLocals = true;

struct Analyzer {
    const CompilerContext& ctx;
    OwnershipPlan plan;
    std::uint32_t nextId = 1;
    std::uint32_t nextRes = 1;
    std::map<OwnershipId, ResourceId> ownedResource;   // fresh-owned decl id → its singleton resource
    std::map<const DeclStmt*, bool> declFreshOwned;    // pass-1 classification cache
    std::map<OwnershipId, std::string> slotType;       // §7: local id → declared type (string rebind keeps Owned)
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
        if (t.rfind("list<", 0) == 0 && !t.empty() && t.back() == '>') {
            if (!d->initialValue) return true;
            // spec/61 return-ownership for LIST producers: a call to an `owns_return: true`
            // function returns a freshly-built INDEPENDENT list the caller owns (e.g.
            // active_match_ids / take_gone_players → list<string>). Drop it at scope exit
            // (kind-2) unless moved. Matched by the call's trailing identifier, like the model
            // and box cases. MUST stay in lockstep with the list-drop registration in
            // visitDeclStmt. (Assignment / at / get inits are views → left non-owned = safe leak.)
            if (auto* c = dynamic_cast<CallExpr*>(d->initialValue.get())) {
                const std::string& fn = c->functionName;
                size_t dp = fn.rfind('.');
                std::string shortName = (dp == std::string::npos) ? fn : fn.substr(dp + 1);
                if (ctx.ownsReturnMatches(shortName, t)) return true;
            }
            return false;
        }
        // spec/61 §7: managed string-local ownership (kill-switch kManagedStringLocals). ON → every
        // string local owns its value. OFF (current, pending open-match crash fix) → T1-era gate:
        // only an INTERPOLATION or producer-call string init owns a fresh RC string. MUST stay in
        // lockstep with kManagedStringLocals in ast_compiler_stmt.cpp.
        if (t == "string") {
            if (kManagedStringLocals) return true;
            if (d->initialValue) {
                if (dynamic_cast<StringInterpolationExpr*>(d->initialValue.get())) return true;
                if (auto* c = dynamic_cast<CallExpr*>(d->initialValue.get())) {
                    const std::string& fn = c->functionName;
                    size_t dp = fn.rfind('.');
                    std::string shortName = (dp == std::string::npos) ? fn : fn.substr(dp + 1);
                    if (ctx.ownsReturnMatches(shortName, t)) return true;
                }
            }
            return false;
        }
        auto ci = ctx.classes.find(t);
        if (ci != ctx.classes.end() && ci->second.hasHeapFields && ci->second.serNodesVar) {
            if (!d->initialValue) return true;
            if (auto* c = dynamic_cast<CallExpr*>(d->initialValue.get())) {
                // spec/61 §5.1: `{ clone: X }` / `{ copy: X }` produce a FRESH, INDEPENDENT owned
                // value model → owned, dropped at scope exit (else the cloned heap leaks; probes
                // 14/15/34). MUST stay in lockstep with modelOwnedInit in ast_compiler_stmt.cpp.
                if (c->functionName == "sys.clone" || c->functionName == "sys.copy") return true;
                if (endsWith(c->functionName, ".get_copy")) return true;
                // spec/61 return-ownership for VALUE models: a call to an `owns_return: true`
                // function returns a freshly-built INDEPENDENT model (e.g. DeckService.list →
                // DeckListResp in every RPC handler's `resp`). The caller owns it → drop at scope
                // exit (kind-0) unless moved. Matched by trailing identifier AND return type (so a
                // container `.get` or a same-named producer of a different type does not collide).
                const std::string& fn = c->functionName;
                size_t dp = fn.rfind('.');
                std::string shortName = (dp == std::string::npos) ? fn : fn.substr(dp + 1);
                if (ctx.ownsReturnMatches(shortName, t)) return true;
            }
        }
        // spec/61: owned box `decl [x, ptr<T>, INIT]` where x owns the pointee → drop at scope
        // exit (deep-free pointee + free shell) unless moved. Two INIT forms:
        //   (1) `{ new: T }`               — heap-allocates T (calloc) locally.
        //   (2) `{ call: fn(...) }` where fn is `owns_return: true` (return-ownership) — the
        //       callee freshly allocated the box and transfers it (e.g. get_base). Matched by
        //       the call's trailing identifier against ctx.ownsReturnFns (the `.get_copy` suffix
        //       precedent above). Restricted to a plain model/class with heap fields + a
        //       ser-subtree; classgroup containers excluded. MUST stay in lockstep with the
        //       box-drop registration in visitDeclStmt.
        if (t.rfind("ptr<", 0) == 0 && !t.empty() && t.back() == '>' && d->initialValue) {
            bool ownedInit = dynamic_cast<NewExpr*>(d->initialValue.get()) != nullptr;
            if (!ownedInit) {
                if (auto* c = dynamic_cast<CallExpr*>(d->initialValue.get())) {
                    const std::string& fn = c->functionName;
                    size_t dp = fn.rfind('.');
                    std::string shortName = (dp == std::string::npos) ? fn : fn.substr(dp + 1);
                    // Return-type match (not just name) — a `map<K:ptr<Iface>>.get` returns a BORROW
                    // and must NOT be classified as an owned box (the OpenLobby handler double-free:
                    // `handlers.get` collided with the service `get` producers).
                    ownedInit = ctx.ownsReturnMatches(shortName, t);
                }
            }
            if (ownedInit) {
                std::string boxed = t.substr(4, t.size() - 5);
                auto bi = ctx.classes.find(boxed);
                if (bi != ctx.classes.end() && bi->second.hasHeapFields && bi->second.serNodesVar
                    && bi->second.type && !ctx.classGroupContainers.count(boxed)
                    // spec/58: a cascade_class instance (`new MkpEffPool`) holds a BORROW of the
                    // shared, loaded-once cascade buffer in a heap field — every instance points at
                    // the same global rows. A kind-4 box-drop would free that shared buffer, so the
                    // next instance (or a later score call) double-frees it. Exclude cascade classes
                    // from the owned-box drop (they borrow; the global cascade store persists).
                    && !ctx.cascadeClasses.count(boxed))
                    return true;
                // Interface fat-pointer box (owns_return call only; you cannot `{ new: Iface }`):
                // deep-freed by virtual dispatch on the concrete type at the drop site (kind 5).
                // Only interface existence is checked here (fatPtrType is materialized lazily in
                // resolveType — guaranteed set by the time visitDeclStmt registers the kind-5 drop,
                // since it resolves this decl's type first; the codegen side null-guards regardless).
                if (ctx.interfaces.count(boxed) > 0)
                    return true;
            }
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
            slotType[id] = d->typeName;
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
                    // spec/61 §6.2: is the target member a WEAK `ref<T>` field? Then `{ref: x}` (lowered
                    // to bare `x`) stored into it is a BORROW, not a move — the source keeps ownership
                    // and its scope-drop must still fire (probe 08: Zone.owner ref back-edge). Resolve
                    // the field's declared type via the base local's class.
                    bool refField = false;
                    {
                        size_t dot = t.find('.');
                        std::string base = (t.rfind("<.", 0) == 0) ? std::string("this")
                                          : (dot != std::string::npos ? t.substr(0, dot) : std::string());
                        std::string field = (t.rfind("<.", 0) == 0) ? t.substr(2)
                                           : (dot != std::string::npos ? t.substr(dot + 1) : std::string());
                        if (field.find('.') == std::string::npos && !base.empty() && !field.empty()) {
                            std::string bt;
                            auto bid = lookup(base);
                            auto tyIt = slotType.find(bid);
                            if (tyIt != slotType.end()) bt = tyIt->second;
                            if (bt.rfind("ptr<", 0) == 0 || bt.rfind("shared<", 0) == 0) {
                                size_t lt = bt.find('<');
                                if (lt != std::string::npos && bt.back() == '>') bt = bt.substr(lt + 1, bt.size() - lt - 2);
                            }
                            auto ci = ctx.classes.find(bt);
                            if (ci != ctx.classes.end()) {
                                auto mit = ci->second.members.find(field);
                                if (mit != ci->second.members.end()
                                    && mit->second.typeName.rfind("ref<", 0) == 0)
                                    refField = true;
                            }
                        }
                    }
                    Origin srcO = Origin::Unknown;
                    OwnershipId sid = NoOwnership;
                    if (auto* v = dynamic_cast<const VariableExpr*>(set->value.get()))
                        if (v->name.find('.') == std::string::npos && v->name.find('<') == std::string::npos) {
                            sid = lookup(v->name);
                            auto it = st.slots.find(sid);
                            if (it != st.slots.end()) srcO = it->second.origin;
                        }
                    StatementPlan sp;
                    // A ref<T> (weak) target never moves the source; a copy into it is neither Move nor a
                    // deep-copy of an owned heap — treat as DeepCopy (no source consumption downstream).
                    sp.transfer = (!refField && srcO == Origin::Owned) ? Transfer::Move : Transfer::DeepCopy;
                    // §7: a MANAGED STRING local source is SHARED into the member, NOT moved: the codegen
                    // increfs it (ast_compiler_stmt.cpp:3195) and deliberately does NOT consumeLocalDrop it
                    // (:2374), so the source keeps its owning ref + null-safe scope-drop. Classifying it as
                    // Move would strike the source's drop (below) while the codegen increfs → the shared
                    // buffer nets one decref short → 22 B/call leak (handle_validate `out.email = eml`;
                    // repro tests/_mem_sanitize/handle_validate_sibling.aiml). Force DeepCopy so the drop is
                    // kept, mirroring the plain-local-rebind managed-string exception below. Safe (no double-
                    // free): the codegen ALWAYS increfs a VariableExpr string source into a member, so the
                    // kept source-drop + the member's own drop exactly balance the incref.
                    {
                        auto tyIt = slotType.find(sid);
                        if (kManagedStringLocals && sid != NoOwnership
                            && tyIt != slotType.end() && tyIt->second == "string")
                            sp.transfer = Transfer::DeepCopy;
                        // spec/61 §4.2/§4.4.5: a VALUE-MODEL local with heap fields stored into a model
                        // FIELD must be a DEEP COPY (independent), not a shallow Move. A Move shallow-
                        // copies the struct (aliasing the source's container/string heap) and marks the
                        // source Moved (strikes its drop) — but if the FIELD held a previous value
                        // (default-init empty container handles, or a prior assignment) that heap is
                        // orphaned → per-store leak (the match-view `set out.match = view_buf` class,
                        // ~persistent list-handle leak per poll). DeepCopy routes to
                        // aiml_ser_deep_copy_replace (frees the field's old heap first, then decodes an
                        // independent copy); the source keeps its own drop. Safe: two independent heaps,
                        // two balanced drops (no double-free). Refcount models (%M* refs) are excluded —
                        // they rebind by reference (incref-new/decref-old) below, not by value copy.
                        else if (sid != NoOwnership && tyIt != slotType.end()) {
                            auto ci = ctx.classes.find(tyIt->second);
                            if (ci != ctx.classes.end() && ci->second.hasHeapFields
                                && ci->second.serNodesVar && !ctx.isRefcountModel(tyIt->second))
                                sp.transfer = Transfer::DeepCopy;
                        }
                    }
                    plan.statements[set] = sp;
                    // Finding #4: once an owned local is MOVED into a member it no longer owns its
                    // heap — mark it moved so a SECOND member-store of the same local classifies as
                    // DeepCopy (independent), not another shallow Move that would share + double-free.
                    // A ref<T> weak target does NOT move → the source stays Owned+Live (probe 08).
                    if (!refField && sp.transfer == Transfer::Move && sid != NoOwnership) {
                        auto it = st.slots.find(sid);
                        if (it != st.slots.end()) { it->second.origin = Origin::Unknown; it->second.life = Moved; }
                    }
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
                        auto tyIt = slotType.find(id);
                        bool isString = kManagedStringLocals
                            && (tyIt != slotType.end() && tyIt->second == "string");
                        if (isString) {
                            // §7: a managed string local stays Owned across a rebind — codegen
                            // dup's a borrowed RHS / moves an owned temp, so the slot always holds an
                            // independent RC ref; its null-safe scope-drop must still fire on the
                            // final value. Restore Owned + its stable decl resource (assigned in
                            // pass 1 because freshOwned(string)=true), keeping the fixpoint stable.
                            auto res = ownedResource.find(id);
                            if (it != st.slots.end() && res != ownedResource.end()) {
                                it->second.origin = Origin::Owned;
                                it->second.life = Live;
                                it->second.resources.insert(res->second);
                            }
                        } else if (it != st.slots.end() && it->second.origin == Origin::Owned) {
                            it->second.origin = Origin::Unknown;   // rebound → not provably owned
                        }
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
