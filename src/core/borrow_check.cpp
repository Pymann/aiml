// spec/61 §10 — conservative, memV3-only borrow/ownership diagnostics. See borrow_check.h.
#include "borrow_check.h"
#include "ast_nodes.h"
#include "context.h"
#include "../../logger.h"   // LOG_ERROR (fails compilation)
#include <map>
#include <string>

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

    explicit BorrowChecker(const CompilerContext& c) : ctx(c) {}

    static std::string loc(int line) {
        return line > 0 ? ("line " + std::to_string(line) + ": ") : std::string();
    }
    bool tracked(const std::string& n) const { return ptrLocals.count(n) != 0; }

    void flagUse(const std::string& n, int line, const char* how) {
        auto it = state.find(n);
        if (it == state.end()) return;
        if (it->second == St::Moved)
            LOG_ERROR(loc(line) + "use-after-move: '" + n + "' was moved out and is used here ("
                + how + ") — a ptr<T> has a single owner (§6.2/§10). Re-bind it (set/new) first.");
        else if (it->second == St::Freed)
            LOG_ERROR(loc(line) + "use-after-free: '" + n + "' was freed and is used here ("
                + how + ") (§10).");
    }

    // Scan an expression for READS of tracked ptr locals in a bad state.
    void scanUses(const Expr* e, int line) {
        if (!e) return;
        if (auto* v = dynamic_cast<const VariableExpr*>(e)) {
            std::string b = baseName(v->name);
            if (tracked(b)) flagUse(b, line, "read");
            return;
        }
        if (auto* b = dynamic_cast<const BinaryExpr*>(e)) { scanUses(b->lhs.get(), line); scanUses(b->rhs.get(), line); return; }
        if (auto* u = dynamic_cast<const UnaryExpr*>(e)) {
            // `free`/`clone`/`copy` unary forms are handled at the statement level; a bare unary
            // (deref `?p`, addr `&p`, `!x`) is a read of its operand.
            scanUses(u->operand.get(), line); return;
        }
        if (auto* c = dynamic_cast<const CallExpr*>(e)) { for (auto& a : c->args) scanUses(a.get(), line); return; }
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
            // Register the new local (shadowing is disallowed elsewhere). ptr<T> → tracked, Live.
            if (ptrTgt) { ptrLocals[d->varName] = d->typeName; state[d->varName] = St::Live; }
            return;
        }
        if (auto* set = dynamic_cast<const SetStmt*>(s)) {
            int line = set->sourceLine;
            scanUses(set->value.get(), line);
            std::string tb = baseName(set->target);
            bool plainTarget = (set->target.find_first_of(".-<") == std::string::npos);
            // A member/index target (`p.child = …`) writes THROUGH p → that is a USE of p.
            if (!plainTarget && tracked(tb)) flagUse(tb, line, "write through");
            // rebind of a ptr local from another ptr local (target is ptr<T> or shared<T>): move src.
            if (plainTarget && tracked(tb)) {
                std::string src = asPtrVar(set->value.get());
                if (!src.empty() && src != tb) markMoved(src, line);
                state[tb] = St::Live;          // rebind (incl. self-assign) revives the slot
            } else if (plainTarget) {
                // target is some other local possibly of ptr/shared type — a move of a ptr source.
                std::string src = asPtrVar(set->value.get());
                if (!src.empty() && src != tb) markMoved(src, line);
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
            return;
        }
        if (auto* es = dynamic_cast<const ExprStmt*>(s)) { exprStmt(es->expr.get(), es->sourceLine); return; }
        if (auto* i = dynamic_cast<const IfStmt*>(s)) {
            int line = i->sourceLine;
            scanUses(i->condition.get(), line);
            std::map<std::string, St> pre = state;
            run(i->thenBranch.get());
            std::map<std::string, St> afterThen = state;
            state = pre;
            if (i->elseBranch) run(i->elseBranch.get());
            std::map<std::string, St> afterElse = state;
            // Merge: a state is carried past the if only if it holds on EVERY path (must-move/free).
            // No else ⇒ the fall-through path keeps `pre`, so nothing bad is carried forward.
            state = pre;
            if (i->elseBranch) {
                for (auto& kv : ptrLocals) {
                    const std::string& n = kv.first;
                    St a = afterThen.count(n) ? afterThen[n] : St::Live;
                    St b = afterElse.count(n) ? afterElse[n] : St::Live;
                    if (a == b && a != St::Live) state[n] = a;
                }
            }
            return;
        }
        if (auto* w = dynamic_cast<const WhileStmt*>(s)) {
            int line = w->sourceLine;
            scanUses(w->condition.get(), line);
            std::map<std::string, St> pre = state;
            run(w->body.get());       // catch in-body violations (from the pre-loop state)
            state = pre;              // conservative: body moves do not escape the loop
            return;
        }
        if (auto* t = dynamic_cast<const TryStmt*>(s)) {
            std::map<std::string, St> pre = state;
            run(t->tryBlock.get());
            state = pre;
            run(t->catchBlock.get());
            state = pre;              // conservative: exceptions make branch effects uncertain
            return;
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
        // A method/function call: scan args for bad reads, then apply `consumed`-param moves and
        // container push/append moves (both are genuine moves of a ptr local).
        if (auto* c = dynamic_cast<const CallExpr*>(e)) {
            for (auto& a : c->args) scanUses(a.get(), line);
            const std::string& fn = c->functionName;
            size_t dot = fn.rfind('.');
            std::string method = (dot == std::string::npos) ? fn : fn.substr(dot + 1);
            if (method == "push" || method == "append" || method == "add") {
                for (auto& a : c->args) { std::string src = asPtrVar(a.get()); if (!src.empty()) markMoved(src, line); }
                return;
            }
            // consumed parameters (free functions): argLifetimes is parallel to the params.
            auto fit = ctx.functions.find(fn);
            if (fit != ctx.functions.end() && !fit->second.argLifetimes.empty()) {
                const auto& lifos = fit->second.argLifetimes;
                for (size_t j = 0; j < c->args.size() && j < lifos.size(); ++j)
                    if (lifos[j] == "consumed") { std::string src = asPtrVar(c->args[j].get()); if (!src.empty()) markMoved(src, line); }
            }
            return;
        }
        scanUses(e, line);
    }
};

} // namespace

void checkBorrowsMemV3(const ::CompilerContext& ctx, const FunctionDecl& fn) {
    if (!ctx.memV3 || !fn.body) return;
    BorrowChecker bc(ctx);
    // Parameters are borrows by default (§8) — do not track them as owned ptr locals.
    bc.run(fn.body.get());
}

} } // namespace AIML::AST
