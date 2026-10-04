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
        if ((t.rfind("list<", 0) == 0 && !t.empty() && t.back() == '>')
            || (ctx.memV3 && ctx.isMemV3ContainerHandle(t))) {
            if (!d->initialValue) return true;
            // spec/61 §5/§5.7: list/map/buffer locals own one handle reference.
            // Their emit*HandleDecl paths register matching scope drops. In particular map/buffer
            // fall-through must not suppress the source drop after a member SHARE.
            // A list local ALWAYS owns exactly one handle ref — emitListHandleDecl
            // (ast_compiler_stmt.cpp) increfs a SHARE source (a plain local, a model member, or an
            // at-element) or adopts a FRESH handle (copy/clone/slice/owns_return) and registers a
            // balancing kind-2 scope-drop. That drop is mayDrop-gated at fall-through, so any init
            // form left non-owned here has its drop SUPPRESSED → one leaked ref per call
            // (leak_probes/round6_general §5: list_member_read/slice/copy). Classify every OWNING
            // init as fresh-owned so the drop fires; mayDrop still suppresses genuinely moved-out
            // handles → no double-free. (`return` was already clean: emitReturnCleanup exempts
            // isListHandle unconditionally, which is why a `return: []` variant masked the leak.)
            Expr* iv = d->initialValue.get();
            // SHARE: incref of a VISIBLE handle (plain-local alias, `obj.field`, at-element) →
            // the source keeps its own ref, this local owns an INDEPENDENT one → safe to drop.
            if (dynamic_cast<VariableExpr*>(iv)) return true;
            if (dynamic_cast<AtExpr*>(iv)) return true;
            // spec/61 §8: a container return is always an owning reference (copy/clone/slice fresh,
            // accessors incref'd at the decl, any other call moves its ref) → the caller drops it.
            if (dynamic_cast<CallExpr*>(iv)) return true;
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
            // spec/61 §5.4/§7 CopyRetain (endphase-crash fix): a value-model materialised from a
            // sharing VIEW (at / obj.field / a container accessor) is OWNED — codegen retains its
            // shareable field handles so x holds its own refs; it must drop them at scope exit (else a
            // later member-store's release-old freed a handle the container still referenced:
            // mana_choice double-free). Lockstep with is_view_alias retain/drop in ast_compiler_stmt.cpp.
            if (dynamic_cast<const AtExpr*>(d->initialValue.get())) return true;
            if (auto* ve = dynamic_cast<const VariableExpr*>(d->initialValue.get()))
                if (ve->name.find('.') != std::string::npos || ve->name.find('<') != std::string::npos)
                    return true;
            if (auto* c = dynamic_cast<CallExpr*>(d->initialValue.get())) {
                // spec/61 §5.1: `{ clone: X }` / `{ copy: X }` produce a FRESH, INDEPENDENT owned
                // value model → owned, dropped at scope exit (else the cloned heap leaks; probes
                // 14/15/34). MUST stay in lockstep with modelOwnedInit in ast_compiler_stmt.cpp.
                if (c->functionName == "sys.clone" || c->functionName == "sys.copy") return true;
                if (endsWith(c->functionName, ".get_copy")) return true;
                // spec/61 §4 (round5 Finding B / live-match server leak): `sys.deserialize(json, "T")`
                // decodes a FRESH, INDEPENDENT owned value model (ser_decode allocates its string/
                // container heap). Without an owned classification no scope-drop fires → the decoded
                // heap leaks 71 B/call (probe class2_deser). get_copy (also a decode) proves the kind-0
                // model-drop releases the decoded heap cleanly → classify sys.deserialize the same.
                // MUST stay in lockstep with modelOwnedInit in ast_compiler_stmt.cpp.
                if (c->functionName == "sys.deserialize") return true;
                // spec/61 return-ownership for VALUE models: a call to an `owns_return: true`
                // function returns a freshly-built INDEPENDENT model (e.g. DeckService.list →
                // DeckListResp in every RPC handler's `resp`). The caller owns it → drop at scope
                // exit (kind-0) unless moved. Matched by trailing identifier AND return type (so a
                // container `.get` or a same-named producer of a different type does not collide).
                const std::string& fn = c->functionName;
                size_t dp = fn.rfind('.');
                std::string shortName = (dp == std::string::npos) ? fn : fn.substr(dp + 1);
                if (ctx.ownsReturnMatches(shortName, t)) return true;
                if ((shortName == "at" || shortName == "get" || shortName == "first" || shortName == "last")
                    && !ctx.ownsReturnMatches(shortName, t))
                    return true;   // CopyRetain view-accessor (lockstep with is_view_alias)
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
                // spec/61 §6.2 (round8 §4): a POD box — a plain class with NO heap fields — still
                // OWNS its outer `new` allocation and gets a SHELL-ONLY kind-4 drop. Classify it as
                // fresh-owned so that drop is not suppressed. classgroup/cascade borrows excluded.
                // MUST stay in lockstep with the box-drop registration in visitDeclStmt.
                if (bi != ctx.classes.end() && bi->second.type
                    && !ctx.classGroupContainers.count(boxed) && !ctx.cascadeClasses.count(boxed))
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
        } else if (auto* t = dynamic_cast<const TryStmt*>(s)) {
            assignIds(t->tryBlock.get());
            assignIds(t->catchBlock.get());
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
    // `set [local, rhs]` on a heap value-model local yields an INDEPENDENT value: visitSetStmt deep-copies
    // a same-type plain local, an `at` element, an at/get/first/last view call or a member access, and
    // moves a fresh owned call result.
    bool rebindDeepCopies(const std::string& type, const Expr* rhs) {
        auto ci = ctx.classes.find(type);
        if (ci == ctx.classes.end() || !ci->second.hasHeapFields || !ci->second.serNodesVar
            || ctx.isRefcountModel(type))
            return false;
        if (dynamic_cast<const AtExpr*>(rhs)) return true;
        if (auto* ce = dynamic_cast<const CallExpr*>(rhs)) {
            const std::string& fn = ce->functionName;
            size_t dp = fn.rfind('.');
            std::string sn = (dp == std::string::npos) ? fn : fn.substr(dp + 1);
            // a FRESH owned result (producer / clone / copy / deserialize) is moved in (tests/lifetime/75)
            if (fn == "sys.clone" || fn == "sys.copy" || fn == "sys.deserialize" || sn == "get_copy"
                || ctx.ownsReturnMatches(sn, type))
                return true;
            return (sn == "at" || sn == "get" || sn == "first" || sn == "last");
        }
        if (auto* ve = dynamic_cast<const VariableExpr*>(rhs)) {
            if (ve->name.find('.') != std::string::npos || ve->name.find('<') != std::string::npos)
                return true;
            OwnershipId rid = lookup(ve->name);
            auto rt = slotType.find(rid);
            return rid != NoOwnership && rt != slotType.end() && rt->second == type;
        }
        return false;
    }

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
        for (const auto& s : block->statements) {
            plan.stmtEntry[s.get()] = st;
            st = analyzeStmt(s.get(), std::move(st), blockLocals);
        }
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
                            // spec/61 §5: named container assignment shares its handle.
                            // The source stays live; codegen retains before releasing the old field.
                            else if (ctx.memV3 && ctx.isMemV3ContainerHandle(tyIt->second))
                                sp.transfer = Transfer::Share;
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
                        // spec/61 §5: a container-handle local rebound from another plain local shares
                        // (codegen increfs) → the slot holds its own reference and keeps its drop.
                        bool handleShare = false;
                        if (ctx.memV3 && tyIt != slotType.end() && ctx.isMemV3ContainerHandle(tyIt->second))
                            if (auto* rv = dynamic_cast<const VariableExpr*>(set->value.get()))
                                if (rv->name.find('.') == std::string::npos && rv->name.find('<') == std::string::npos) {
                                    auto rt = slotType.find(lookup(rv->name));
                                    handleShare = rt != slotType.end() && ctx.isMemV3ContainerHandle(rt->second);
                                }
                        if (isString || handleShare) {
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
                        } else if (it != st.slots.end()) {
                            // spec/61 §7: a view / same-type local RHS is DEEP-COPIED into the local
                            // (codegen), so the local owns its new value: it stays Owned and releases
                            // the old one (tests/lifetime/71, 72). Any other RHS may share → Unknown.
                            bool indep = tyIt != slotType.end() && rebindDeepCopies(tyIt->second, set->value.get());
                            // spec/61 §5.5: an owned ptr<T> box rebound to a fresh box stays Owned
                            // (codegen releases the old box, the new one is dropped at scope exit).
                            bool keepBoxOwned = false;
                            if (tyIt != slotType.end() && tyIt->second.rfind("ptr<", 0) == 0) {
                                bool freshBox = dynamic_cast<const NewExpr*>(set->value.get()) != nullptr;
                                if (auto* fc = dynamic_cast<const CallExpr*>(set->value.get())) {
                                    size_t dp = fc->functionName.rfind('.');
                                    std::string sn = (dp == std::string::npos) ? fc->functionName : fc->functionName.substr(dp + 1);
                                    freshBox = ctx.ownsReturnMatches(sn, tyIt->second);
                                }
                                keepBoxOwned = freshBox && it->second.origin == Origin::Owned;
                            }
                            plan.statements[set].rebindReleaseOld = indep && it->second.origin == Origin::Owned;
                            if (!indep && !keepBoxOwned && it->second.origin == Origin::Owned)
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
        if (auto* t = dynamic_cast<const TryStmt*>(s)) {
            // the catch may be entered from any point of the try body → start from the join
            FlowState done = t->tryBlock ? analyzeBlock(t->tryBlock.get(), st) : st;
            if (!t->catchBlock) return done;
            FlowState caught = analyzeBlock(t->catchBlock.get(), joinStates(st, done));
            return joinStates(done, caught);
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
        for (size_t i = 0; i < paramIds.size(); ++i) {
            OwnershipId id = paramIds[i];
            plan.params[fn.args[i].first] = id;
            const std::string& t = fn.args[i].second;
            // spec/61 §8: a ptr<T> parameter is moved in — the callee owns the box
            if (t.size() > 5 && t.rfind("ptr<", 0) == 0 && t.back() == '>') {
                ResourceId r = nextRes++;
                ownedResource[id] = r;
                entry.slots[id] = OwnFact{Origin::Owned, Live, {r}};
            } else {
                entry.slots[id] = OwnFact{Origin::Borrowed, Live, {}};
            }
        }
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
