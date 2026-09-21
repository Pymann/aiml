// spec/61 §10 — conservative, memV3-only borrow/ownership diagnostics. See borrow_check.h.
#include "borrow_check.h"
#include "ast_nodes.h"
#include "context.h"
#include "../../logger.h"   // LOG_ERROR (fails compilation)
#include <map>
#include <set>
#include <vector>
#include <string>
#include <functional>
#include <cstdio>
#include <cstdlib>

namespace AIML { namespace AST {
namespace {

enum class St { Live, Moved, Freed };

// base name of a possibly-dotted/indexed target ("p.child" → "p", "p-0" → "p").
static std::string baseName(const std::string& n) {
    size_t c = n.find_first_of(".-<");
    return (c == std::string::npos) ? n : n.substr(0, c);
}

struct BorrowChecker {
    const CompilerContext& ctx;
    // Only plain `ptr<T>` locals are tracked. name → declared type.
    std::map<std::string, std::string> ptrLocals;
    // Flow state for tracked locals. Absent ⇒ Live (or not yet declared).
    std::map<std::string, St> state;
    // §10 leak lint: `persistent`-qualified ptr locals + their decl line.
    std::map<std::string, int> persistentPtrs;
    // §5.4 invalidated element borrow: a ref<T> local bound from at/get on a container → the
    // container it borrows from; and whether that borrow has been invalidated by a structural
    // mutation of the container. refInvalid participates in the if/else/loop/try flow merge.
    std::map<std::string, std::string> refBorrowOf;   // ref local → container base name
    std::map<std::string, bool> refInvalid;           // ref local → invalidated?
    std::map<std::string, std::string> containerType; // container local → declared type
    // §8 borrow-return anchor: the declared `borrows:` param and the set of ref<T> params. A
    // `return <refParam>` whose param is not the anchor is a wrong-anchor borrow return.
    std::string returnAnchor;
    std::set<std::string> refParams;
    // F3 (§8/§5.4): declared type of every local and parameter, keyed by base name. A missing
    // entry in `ptrLocals` does NOT prove an owning target — a `ref<T>` target must borrow, not
    // move. declTypes lets the SetStmt branch classify the target slot reliably.
    std::map<std::string, std::string> declTypes;
    bool isOwningTargetType(const std::string& t) const {
        return t.rfind("ptr<", 0) == 0 || t.rfind("shared<", 0) == 0;
    }
    bool isRefTargetType(const std::string& t) const { return t.rfind("ref<", 0) == 0; }

    // §5.4: a POINTER-element container (list<ptr/shared/ref>, map<:ptr/shared/ref>) stores
    // pointer slots; growth (push/insert) moves the SLOTS but not the separately-allocated
    // pointees, so an element borrow (which points at the pointee) survives growth. An
    // INLINE-element container (list<i32>, list<ValueModel>, buffer) moves the elements
    // themselves on growth → the borrow dangles.
    bool isPointerElementContainer(const std::string& t) const {
        std::string inner;
        if (t.rfind("list<", 0) == 0 && t.back() == '>') inner = t.substr(5, t.size() - 6);
        else if (t.rfind("cascade<", 0) == 0) return false;      // list<i32> layout — inline
        else if (t.rfind("map<", 0) == 0 && t.back() == '>') {   // value type after ':'
            std::string i = t.substr(4, t.size() - 5); size_t c = i.find(':');
            inner = (c == std::string::npos) ? std::string() : i.substr(c + 1);
        } else return false;                                     // buffer etc.
        return inner.rfind("ptr<", 0) == 0 || inner.rfind("shared<", 0) == 0 || inner.rfind("ref<", 0) == 0;
    }

    std::string fnName;   // enclosing function (for multi-file diagnostics)
    explicit BorrowChecker(const CompilerContext& c) : ctx(c) {}

    std::string loc(int line) const {
        std::string p = fnName.empty() ? std::string() : (fnName + " ");
        return p + (line > 0 ? ("line " + std::to_string(line) + ": ") : std::string());
    }
    bool tracked(const std::string& n) const { return ptrLocals.count(n) != 0; }

    void flagUse(const std::string& n, int line, const char* how) {
        auto it = state.find(n);
        if (it != state.end()) {
            if (it->second == St::Moved)
                LOG_ERROR(loc(line) + "use-after-move: '" + n + "' was moved out and is used here ("
                    + how + ") — a ptr<T> has a single owner (§6.2/§10). Re-bind it (set/new) first.");
            else if (it->second == St::Freed)
                LOG_ERROR(loc(line) + "use-after-free: '" + n + "' was freed and is used here ("
                    + how + ") (§10).");
        }
        // §5.4: an element borrow used after its container was structurally mutated is dangling.
        auto ri = refInvalid.find(n);        if (ri != refInvalid.end() && ri->second)
            LOG_ERROR(loc(line) + "invalidated element borrow: '" + n + "' borrows an element of '"
                + refBorrowOf[n] + "', which was structurally mutated (push/insert/remove/clear/…) "
                "since the borrow — the ref may dangle (§5.4/§10). Re-borrow after the mutation.");
    }

    // If `e` is `{at:[C,..]}` / `{get:[C,..]}` / `C.at(..)` / `C.get(..)` etc., return C's base.
    std::string borrowSourceContainer(const Expr* e) {
        if (auto* at = dynamic_cast<const AtExpr*>(e))
            if (auto* cv = dynamic_cast<const VariableExpr*>(at->container.get())) return baseName(cv->name);
        if (auto* c = dynamic_cast<const CallExpr*>(e)) {
            const std::string& fn = c->functionName;
            size_t dot = fn.rfind('.');
            if (dot != std::string::npos) {
                std::string method = fn.substr(dot + 1);
                if (method == "at" || method == "get" || method == "first" || method == "last")
                    return baseName(fn.substr(0, dot));
            }
        }
        return "";
    }

    // Scan an expression for READS of tracked ptr locals in a bad state.
    void scanUses(const Expr* e, int line) {
        if (!e) return;
        if (auto* v = dynamic_cast<const VariableExpr*>(e)) {
            std::string b = baseName(v->name);
            if (tracked(b) || refBorrowOf.count(b)) flagUse(b, line, "read");
            return;
        }
        if (auto* b = dynamic_cast<const BinaryExpr*>(e)) { scanUses(b->lhs.get(), line); scanUses(b->rhs.get(), line); return; }
        if (auto* u = dynamic_cast<const UnaryExpr*>(e)) {
            // `free`/`clone`/`copy` unary forms are handled at the statement level; a bare unary
            // (deref `?p`, addr `&p`, `!x`) is a read of its operand.
            scanUses(u->operand.get(), line); return;
        }
        if (auto* c = dynamic_cast<const CallExpr*>(e)) { for (auto& a : c->args) scanUses(a.get(), line); applyCallEffects(*c, line); return; }
        if (auto* s = dynamic_cast<const SpawnExpr*>(e)) { for (auto& a : s->args) scanUses(a.get(), line); return; }
        if (auto* at = dynamic_cast<const AtExpr*>(e)) { scanUses(at->container.get(), line); for (auto& i : at->indices) scanUses(i.get(), line); return; }
        if (auto* si = dynamic_cast<const StringInterpolationExpr*>(e)) { for (auto& p : si->parts) scanUses(p.get(), line); return; }
        if (auto* ne = dynamic_cast<const NewExpr*>(e)) { for (auto& kv : ne->initValues) scanUses(kv.second.get(), line); return; }
        if (auto* na = dynamic_cast<const NewArrayExpr*>(e)) { scanUses(na->countExpr.get(), line); return; }
        if (auto* dl = dynamic_cast<const DictLiteralExpr*>(e)) { for (auto& kv : dl->entriesExpr) scanUses(kv.second.get(), line); return; }
    }

    // If `e` is a bare tracked-ptr variable, return its name; else "".
    std::string asPtrVar(const Expr* e) {
        if (auto* v = dynamic_cast<const VariableExpr*>(e))
            if (tracked(v->name)) return v->name;
        return "";
    }

    void markMoved(const std::string& n, int line) {
        // Moving an already-dead ptr is itself a use of a dead value.
        flagUse(n, line, "moved again");
        state[n] = St::Moved;
    }

    // ── statement flow ────────────────────────────────────────────────────────
    void run(const BlockStmt* block) { if (block) for (auto& s : block->statements) stmt(s.get()); }

    void stmt(const Stmt* s) {
        if (auto* d = dynamic_cast<const DeclStmt*>(s)) {
            int line = d->sourceLine;
            scanUses(d->initialValue.get(), line);
            // A ptr/shared target adopting a ptr source is a MOVE of the source (§6.2). A ref
            // target is a borrow (no move). {share: p} is lowered to the bare `p` before here.
            bool ptrTgt = d->typeName.rfind("ptr<", 0) == 0;
            bool sharedTgt = d->typeName.rfind("shared<", 0) == 0;
            if ((ptrTgt || sharedTgt) && d->initialValue) {
                std::string src = asPtrVar(d->initialValue.get());
                if (!src.empty()) markMoved(src, line);
            }
            declTypes[d->varName] = d->typeName;   // F3: reliable target-type resolution
            // Register the new local (shadowing is disallowed elsewhere). ptr<T> → tracked, Live.
            if (ptrTgt) {
                ptrLocals[d->varName] = d->typeName; state[d->varName] = St::Live;
                if (d->lifetime == "persistent") persistentPtrs[d->varName] = line;
            }
            // §5.4: a ref<T> local bound from at/get on a container records the borrow (Valid).
            if (d->typeName.rfind("ref<", 0) == 0 && d->initialValue) {
                std::string cont = borrowSourceContainer(d->initialValue.get());
                if (!cont.empty()) { refBorrowOf[d->varName] = cont; refInvalid[d->varName] = false; }
            }
            // §5.4: remember container local types so mutation can differentiate inline vs pointer
            // element containers (growth invalidates borrows only for inline-element containers).
            if ((d->typeName.rfind("list<", 0) == 0 || d->typeName.rfind("map<", 0) == 0
                 || d->typeName.rfind("cascade<", 0) == 0 || d->typeName == "buffer"
                 || d->typeName.rfind("buffer<", 0) == 0) && d->typeName.back() == '>')
                containerType[d->varName] = d->typeName;
            else if (d->typeName == "buffer") containerType[d->varName] = d->typeName;
            return;
        }
        if (auto* set = dynamic_cast<const SetStmt*>(s)) {
            int line = set->sourceLine;
            scanUses(set->value.get(), line);
            std::string tb = baseName(set->target);
            bool plainTarget = (set->target.find_first_of(".-<") == std::string::npos);
            // A member/index target (`p.child = …`) writes THROUGH p → that is a USE of p.
            if (!plainTarget && tracked(tb)) flagUse(tb, line, "write through");
            // Storing a ptr local INTO a member/container slot moves it (§7 aggregate ownership).
            if (!plainTarget) { std::string src = asPtrVar(set->value.get()); if (!src.empty()) markMoved(src, line); }
            // rebind of a ptr local from another ptr local (target is ptr<T> or shared<T>): move src.
            if (plainTarget && tracked(tb)) {
                std::string src = asPtrVar(set->value.get());
                if (!src.empty() && src != tb) markMoved(src, line);
                state[tb] = St::Live;          // rebind (incl. self-assign) revives the slot
            } else if (plainTarget) {
                // F3 (§8/§5.4): classify the target by its DECLARED type — a missing ptrLocals
                // entry is not proof of an owning slot. Only ptr<T>/shared<T> targets move the
                // source; a `ref<T>` target BORROWS (use-check the source, never move it); other
                // (value) targets do not move a pointer at all.
                std::string tgtType = declTypes.count(tb) ? declTypes[tb] : std::string();
                std::string src = asPtrVar(set->value.get());
                if (isRefTargetType(tgtType)) {
                    // `set: [view, owner]` — view : ref<T> borrows owner; owner stays a valid owner.
                    if (!src.empty()) flagUse(src, line, "borrow");
                } else if (isOwningTargetType(tgtType) || tgtType.empty()) {
                    // Owning rebind, or an unknown slot (conservative: keep the legacy move so real
                    // owning aggregates not captured in declTypes still diagnose double ownership).
                    if (!src.empty() && src != tb) markMoved(src, line);
                }
            }
            // §5.4: re-binding a ref local (set:[r, {at:[C,i]}]) refreshes the borrow → Valid again.
            if (plainTarget && refBorrowOf.count(tb)) {
                std::string cont = borrowSourceContainer(set->value.get());
                if (!cont.empty()) { refBorrowOf[tb] = cont; refInvalid[tb] = false; }
            }
            // member/index target (p.child = …) is a write THROUGH p → scanUses already flagged it.
            return;
        }
        if (auto* st = dynamic_cast<const StoreStmt*>(s)) {
            int line = st->sourceLine;
            if (tracked(st->target)) flagUse(st->target, line, "deref-write");
            scanUses(st->value.get(), line);
            return;
        }
        if (auto* ix = dynamic_cast<const IdxStmt*>(s)) {
            int line = ix->sourceLine;
            std::string cb = baseName(ix->container);
            if (tracked(cb)) flagUse(cb, line, "indexed-write");
            for (auto& i : ix->indices) scanUses(i.get(), line);
            scanUses(ix->value.get(), line);
            return;
        }
        if (auto* r = dynamic_cast<const RetStmt*>(s)) {
            int line = r->sourceLine;
            scanUses(r->value.get(), line);
            if (r->value) { std::string src = asPtrVar(r->value.get()); if (!src.empty()) state[src] = St::Moved; }
            // §8 (probe n10): a `borrows: <anchor>` function must return a borrow of THAT param.
            // Returning a different ref<T> param is a wrong-anchor borrow return (would hand out a
            // borrow whose lifetime is tied to the wrong owner).
            if (!returnAnchor.empty() && r->value)
                if (auto* rv = dynamic_cast<const VariableExpr*>(r->value.get())) {
                    std::string b = baseName(rv->name);
                    if (refParams.count(b) && b != returnAnchor)
                        LOG_ERROR(loc(line) + "borrow-return anchor mismatch: this function declares "
                            "`borrows: " + returnAnchor + "` but returns a borrow derived from '" + b +
                            "' — the returned ref<T>'s lifetime is anchored to the wrong parameter (§8). "
                            "Return a borrow of '" + returnAnchor + "', or change the `borrows:` anchor.");
                }
            return;
        }
        if (auto* es = dynamic_cast<const ExprStmt*>(s)) { exprStmt(es->expr.get(), es->sourceLine); return; }
        if (auto* i = dynamic_cast<const IfStmt*>(s)) {
            int line = i->sourceLine;
            scanUses(i->condition.get(), line);
            auto pre = state; auto preRef = refInvalid;
            run(i->thenBranch.get());
            auto afterThen = state; auto afterThenRef = refInvalid;
            state = pre; refInvalid = preRef;
            if (i->elseBranch) run(i->elseBranch.get());
            auto afterElse = state; auto afterElseRef = refInvalid;
            // Merge: a bad state is carried past the if only if it holds on EVERY path (must-*).
            // No else ⇒ the fall-through path keeps `pre`, so nothing bad is carried forward.
            state = pre; refInvalid = preRef;
            if (i->elseBranch) {
                for (auto& kv : ptrLocals) {
                    const std::string& n = kv.first;
                    St a = afterThen.count(n) ? afterThen[n] : St::Live;
                    St b = afterElse.count(n) ? afterElse[n] : St::Live;
                    if (a == b && a != St::Live) state[n] = a;
                }
                for (auto& kv : refBorrowOf) {
                    const std::string& n = kv.first;
                    bool a = afterThenRef.count(n) && afterThenRef[n];
                    bool b = afterElseRef.count(n) && afterElseRef[n];
                    if (a && b) refInvalid[n] = true;
                }
            }
            return;
        }
        if (auto* w = dynamic_cast<const WhileStmt*>(s)) {
            int line = w->sourceLine;
            scanUses(w->condition.get(), line);
            auto pre = state; auto preRef = refInvalid;
            run(w->body.get());       // catch in-body violations (from the pre-loop state)
            state = pre; refInvalid = preRef;   // conservative: body effects do not escape the loop
            return;
        }
        if (auto* t = dynamic_cast<const TryStmt*>(s)) {
            auto pre = state; auto preRef = refInvalid;
            run(t->tryBlock.get());
            state = pre; refInvalid = preRef;
            run(t->catchBlock.get());
            state = pre; refInvalid = preRef;   // conservative: exceptions make branch effects uncertain
            return;
        }
    }

    // round4 §4: the ownership effects of a call — container growth/reorder borrow-invalidation,
    // push/append/add MOVES, and free-function ptr-param / `consumed` MOVES. Called from scanUses so
    // EVERY call (statement, DeclStmt initializer, assignment RHS, return, nested argument) applies
    // its moves exactly once — previously only statement-level calls (exprStmt) did, so a move inside
    // an initializer (`decl:[x,T,{call:fn(p)}]`) went untracked (neg n12).
    void applyCallEffects(const CallExpr& c, int line) {
        const std::string& fn = c.functionName;
        if (fn == "free") return;   // `free` is handled as a statement in exprStmt, not a move here
        size_t dot = fn.rfind('.');
        std::string method   = (dot == std::string::npos) ? fn : fn.substr(dot + 1);
        std::string recvBase = (dot == std::string::npos) ? "" : baseName(fn.substr(0, dot));
        // §5.4: growth (push/insert) invalidates element borrows only for INLINE-element containers;
        // remove/clear/sort/reverse invalidate regardless (probe 25).
        if (!recvBase.empty()) {
            bool growth  = (method == "push" || method == "append" || method == "add" || method == "insert");
            bool reorder = (method == "remove" || method == "clear" || method == "sort" || method == "reverse");
            bool ptrElem = isPointerElementContainer(containerType.count(recvBase) ? containerType[recvBase] : "");
            if (reorder || (growth && !ptrElem))
                for (auto& rb : refBorrowOf) if (rb.second == recvBase) refInvalid[rb.first] = true;
        }
        if (method == "push" || method == "append" || method == "add") {
            for (auto& a : c.args) { std::string src = asPtrVar(a.get()); if (!src.empty()) markMoved(src, line); }
            return;
        }
        // §6.2/§8/§3.1 (free functions): a ptr<T> local passed to a by-value ptr<T> param MOVES it
        // (n11); a `consumed` param also moves; ref<T>/shared<T> params do not. argTypeNames tells them apart.
        auto fit = ctx.functions.find(fn);
        if (fit != ctx.functions.end()) {
            const auto& lifos = fit->second.argLifetimes;
            const auto& tys   = fit->second.argTypeNames;
            for (size_t j = 0; j < c.args.size(); ++j) {
                bool consumed = (j < lifos.size() && lifos[j] == "consumed");
                bool ptrParam = (j < tys.size() && tys[j].rfind("ptr<", 0) == 0);
                if (consumed || ptrParam) { std::string src = asPtrVar(c.args[j].get()); if (!src.empty()) markMoved(src, line); }
            }
        }
    }

    void exprStmt(const Expr* e, int line) {
        if (!e) return;
        // `free: [p]` lowers to UnaryExpr("free", p) or CallExpr("free", [p]).
        const Expr* freeArg = nullptr;
        if (auto* u = dynamic_cast<const UnaryExpr*>(e)) { if (u->op == "free") freeArg = u->operand.get(); }
        if (auto* c = dynamic_cast<const CallExpr*>(e)) { if (c->functionName == "free" && !c->args.empty()) freeArg = c->args[0].get(); }
        if (freeArg) {
            std::string p = asPtrVar(freeArg);
            if (!p.empty()) {
                auto it = state.find(p);
                if (it != state.end() && it->second == St::Freed)
                    LOG_ERROR(loc(line) + "double free: '" + p + "' is freed again here (§10).");
                else if (it != state.end() && it->second == St::Moved)
                    LOG_ERROR(loc(line) + "use-after-move: '" + p + "' was moved out, then freed here (§10).");
                state[p] = St::Freed;
                return;
            }
            // freeing a non-tracked expr: still scan it for bad reads.
            scanUses(freeArg, line);
            return;
        }
        // Everything else (including calls) → scanUses scans reads AND applies each call's ownership
        // effects (applyCallEffects) exactly once. Statement-level and initializer-level calls thus
        // share one path (round4 §4); no separate move handling here to avoid double-moving.
        scanUses(e, line);
    }
};

} // namespace

void checkBorrowsMemV3(const ::CompilerContext& ctx, const FunctionDecl& fn) {
    if (!ctx.memV3 || !fn.body) return;
    BorrowChecker bc(ctx);
    bc.fnName = fn.name;
    // §8 borrow-return anchor: record the declared anchor + the ref<T> params for the check.
    bc.returnAnchor = fn.returnBorrowsParam;
    for (const auto& a : fn.args) {
        if (a.second.rfind("ref<", 0) == 0) bc.refParams.insert(a.first);
        bc.declTypes[a.first] = a.second;   // F3: params participate in target-type resolution
    }
    // Parameters are borrows by default (§8) — do not track them as owned ptr locals.
    bc.run(fn.body.get());
    // §10 leak lint: a `persistent` box that reaches function exit still Live never escaped
    // (the declared escape did not happen) → WARNING. `scope` (default) boxes cannot leak — they
    // auto-free. Honours `static_off: ["leak"]`.
    bool leakOff = false;
    for (const auto& so : fn.staticOff) if (so == "leak") leakOff = true;
    if (!leakOff)
        for (const auto& kv : bc.persistentPtrs) {
            auto it = bc.state.find(kv.first);
            if (it != bc.state.end() && it->second == St::Live)
                LOG_WARNING(bc.loc(kv.second) + "leak: persistent box '" + kv.first +
                    "' reaches scope exit without being moved out / stored / returned — its declared "
                    "escape never happens, so it leaks (§9.1/§10). Move it out, or drop `persistent` "
                    "to auto-free at scope. (static_off: [\"leak\"] to silence.)");
        }
}

// ── §10 owning-cycle lint ─────────────────────────────────────────────────────
namespace {
// The owned model types a field of `typeName` transitively references. `ref<T>` is WEAK →
// contributes nothing (it is the cycle-breaker). Descends ptr/shared/list/cascade/buffer/map.
static void collectOwnedModels(const ::CompilerContext& ctx, const std::string& typeName,
                               std::vector<std::string>& out) {
    std::string t = typeName;
    if (t.rfind("ref<", 0) == 0) return;                       // weak edge — not owned
    auto inner = [](const std::string& s, size_t pfx) { return s.substr(pfx, s.size() - pfx - 1); };
    if (t.rfind("ptr<", 0) == 0 && t.back() == '>')    { collectOwnedModels(ctx, inner(t, 4), out); return; }
    if (t.rfind("shared<", 0) == 0 && t.back() == '>') { collectOwnedModels(ctx, inner(t, 7), out); return; }
    if (t.rfind("list<", 0) == 0 && t.back() == '>')   { collectOwnedModels(ctx, inner(t, 5), out); return; }
    if (t.rfind("cascade<", 0) == 0 && t.back() == '>'){ collectOwnedModels(ctx, inner(t, 8), out); return; }
    if (t.rfind("buffer<", 0) == 0 && t.back() == '>') { collectOwnedModels(ctx, inner(t, 7), out); return; }
    if (t.rfind("map<", 0) == 0 && t.back() == '>') {          // values are owned; keys scalar/string
        std::string i = inner(t, 4); size_t c = i.find(':');
        if (c != std::string::npos) collectOwnedModels(ctx, i.substr(c + 1), out);
        return;
    }
    auto it = ctx.classes.find(t);
    if (it != ctx.classes.end() && it->second.isModel && !it->second.isBuiltin)
        out.push_back(t);                                       // an owned model reference
}
} // namespace

void checkOwningCyclesMemV3(const ::CompilerContext& ctx) {
    if (!ctx.memV3) return;
    // Build owning adjacency over model types.
    std::map<std::string, std::vector<std::string>> adj;
    for (const auto& [name, ci] : ctx.classes) {
        if (!ci.isModel || ci.isBuiltin) continue;
        auto& edges = adj[name];
        for (const auto& [mn, mem] : ci.members) {
            (void)mn;
            std::vector<std::string> owned;
            collectOwnedModels(ctx, mem.typeName, owned);
            for (auto& b : owned) if (b != name || true) edges.push_back(b);  // include self-cycles
        }
    }
    // DFS cycle detection (white/gray/black), dedupe warnings by the cycle's member set.
    std::map<std::string, int> color;       // 0 white, 1 gray, 2 black
    std::vector<std::string> stack;
    std::set<std::string> warned;           // sorted-join keys already reported
    std::function<void(const std::string&)> dfs = [&](const std::string& u) {
        color[u] = 1; stack.push_back(u);
        auto it = adj.find(u);
        if (it != adj.end()) for (const std::string& v : it->second) {
            if (color[v] == 1) {
                // back-edge → cycle from v..u on the stack.
                std::vector<std::string> cyc;
                bool in = false;
                for (auto& n : stack) { if (n == v) in = true; if (in) cyc.push_back(n); }
                std::set<std::string> keyset(cyc.begin(), cyc.end());
                std::string key; for (auto& n : keyset) key += n + ",";
                if (warned.insert(key).second) {
                    std::string path; for (size_t i = 0; i < cyc.size(); ++i) path += (i ? " → " : "") + cyc[i];
                    path += " → " + v;
                    LOG_WARNING("spec/61 §10 owning-cycle: " + path + " form a cycle of OWNING edges "
                        "(ptr/shared/container/nested) with no ref<T> to break it — this structure can "
                        "leak (the refcounts never reach 0). Make one back-edge a ref<T> (§5.2).");
                }
            } else if (color[v] == 0) dfs(v);
        }
        color[u] = 2; stack.pop_back();
    };
    for (const auto& [name, ci] : ctx.classes)
        if (ci.isModel && !ci.isBuiltin && color[name] == 0) dfs(name);
}

} } // namespace AIML::AST
