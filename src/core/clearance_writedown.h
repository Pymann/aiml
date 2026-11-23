#pragma once
// spec/65 F65-34 — the write-down rule, OPT-IN like F65-17 (the same lever as the
// flow check: one switch, not two).
//
// A value of level L must not be written into a place that is LESS
// restricted than L.  Otherwise a restricted value is laundered in an assignment:
// read it where one may, store it where anyone may.
//
// Two meanings per local that must be kept apart:
//   `level:`  = the BARRIER for storing — what may go in here.
//   carried   = the strictest thing that ever flowed in — and that travels on.
// That is why a local WITHOUT an annotation carries a level too, as soon as a restricted
// value flows in; without that the rule would be bypassed with `tmp = read_secret()`.
//
// Without an annotation AND without restricted origin a local carries no level — not
// level 0.  That is the difference from F65-5: there 0 is the fail-closed default for
// ACCESS; here every literal in an annotated local would be a violation, and a
// rule that breaks every program protects nothing.
#include <map>
#include <string>
#include "ast_nodes.h"
#include "clearance.h"
#include "../../logger.h"

namespace aiml {
namespace clearance {

using namespace AIML::AST;

// What levels a file knows: its own functions and globals.  ACROSS files
// this covers nothing — the same limit as with the flow check, and for the same
// reason: an imported declaration carries no level.
struct LevelEnv {
    std::map<std::string, int> funcs;
    std::map<std::string, int> globals;
};

// The strictest (= smallest) annotated level that flows into this expression.
// kAbsent means: no restricted origin.
inline int exprLevel(const Expr* e, const LevelEnv& env,
                     const std::map<std::string, int>& carried) {
    if (!e) return kAbsent;
    int best = kAbsent;
    auto take = [&best](int lv) {
        if (lv == kAbsent) return;
        if (best == kAbsent || lv < best) best = lv;
    };
    auto sub = [&](const Expr* x) { take(exprLevel(x, env, carried)); };

    if (auto* v = dynamic_cast<const VariableExpr*>(e)) {
        auto it = carried.find(v->name);
        if (it != carried.end()) take(it->second);
        auto g = env.globals.find(v->name);
        if (g != env.globals.end()) take(g->second);
    } else if (auto* c = dynamic_cast<const CallExpr*>(e)) {
        auto f = env.funcs.find(c->functionName);
        if (f != env.funcs.end()) take(f->second);
        for (const auto& a : c->args) sub(a.get());
    } else if (auto* b = dynamic_cast<const BinaryExpr*>(e)) {
        sub(b->lhs.get()); sub(b->rhs.get());
    } else if (auto* u = dynamic_cast<const UnaryExpr*>(e)) {
        sub(u->operand.get());
    } else if (auto* s = dynamic_cast<const StringInterpolationExpr*>(e)) {
        for (const auto& p : s->parts) sub(p.get());
    } else if (auto* a = dynamic_cast<const AtExpr*>(e)) {
        sub(a->container.get());
        for (const auto& i : a->indices) sub(i.get());
    } else if (auto* sp = dynamic_cast<const SpawnExpr*>(e)) {
        auto f = env.funcs.find(sp->functionName);
        if (f != env.funcs.end()) take(f->second);
        for (const auto& x : sp->args) sub(x.get());
    } else if (auto* n = dynamic_cast<const NewExpr*>(e)) {
        for (const auto& kv : n->initValues) sub(kv.second.get());
    } else if (auto* bl = dynamic_cast<const BufferLiteralExpr*>(e)) {
        for (const auto& x : bl->elements) sub(x.get());
    } else if (auto* d = dynamic_cast<const DictLiteralExpr*>(e)) {
        for (const auto& kv : d->entriesExpr) sub(kv.second.get());
    }
    return best;
}

// One body, one forward pass.  `bound` are the barriers (`level:` on the decl),
// `carried` is what a local carries.  Returns the number of violations.
inline int walkBody(const BlockStmt* blk, const std::string& fname, const LevelEnv& env,
                    std::map<std::string, int>& bound,
                    std::map<std::string, int>& carried) {
    if (!blk) return 0;
    int bad = 0;

    auto carry = [&carried](const std::string& nm, int lv) {
        if (lv == kAbsent) return;
        auto it = carried.find(nm);
        if (it == carried.end() || lv < it->second) carried[nm] = lv;
    };

    auto check = [&](const std::string& dst, const Expr* src) {
        const int from = exprLevel(src, env, carried);
        auto b = bound.find(dst);
        if (b != bound.end() && from != kAbsent && b->second > from) {
            ++bad;
            LOG_ERROR("clearance-writedown: in `" + fname + "` a value of level " +
                      std::to_string(from) + " flows into `" + dst + "`, which is declared at level " +
                      std::to_string(b->second) + " — i.e. into a LESS "
                      "restricted place (spec/65 F65-34).  Either declare `" + dst +
                      "` at level " + std::to_string(from) + " or stricter, "
                      "or do not store the value here.");
        }
        carry(dst, from);
    };

    for (const auto& st : blk->statements) {
        Stmt* s = st.get();
        if (auto* d = dynamic_cast<DeclStmt*>(s)) {
            if (d->level >= 0) bound[d->varName] = d->level;
            if (d->initialValue) check(d->varName, d->initialValue.get());
        } else if (auto* se = dynamic_cast<SetStmt*>(s)) {
            check(se->target, se->value.get());
        } else if (auto* so = dynamic_cast<StoreStmt*>(s)) {
            check(so->target, so->value.get());
        } else if (auto* ix = dynamic_cast<IdxStmt*>(s)) {
            check(ix->container, ix->value.get());
        } else if (auto* iff = dynamic_cast<IfStmt*>(s)) {
            bad += walkBody(iff->thenBranch.get(), fname, env, bound, carried);
            bad += walkBody(iff->elseBranch.get(), fname, env, bound, carried);
        } else if (auto* wh = dynamic_cast<WhileStmt*>(s)) {
            bad += walkBody(wh->body.get(), fname, env, bound, carried);
        } else if (auto* tr = dynamic_cast<TryStmt*>(s)) {
            bad += walkBody(tr->tryBlock.get(), fname, env, bound, carried);
            for (auto* cb : tr->catchBodies()) bad += walkBody(cb, fname, env, bound, carried);
        } else if (auto* bs = dynamic_cast<BlockStmt*>(s)) {
            bad += walkBody(bs, fname, env, bound, carried);
        }
    }
    return bad;
}

// Entry: one file, one pass.  Runs only on explicit request (F65-17) and
// at the same place as the flow check, so that there is ONE lever and not two.
inline int checkWritedown(const Program* prog) {
    if (!prog) return 0;
    LevelEnv env;
    for (const auto& d : prog->declarations) {
        if (auto* fn = dynamic_cast<const FunctionDecl*>(d.get())) {
            if (fn->level >= 0) env.funcs[fn->name] = fn->level;
        } else if (auto* gd = dynamic_cast<const DeclStmt*>(d.get())) {
            if (gd->level >= 0) env.globals[gd->varName] = gd->level;
        }
    }
    int bad = 0;
    for (const auto& d : prog->declarations) {
        auto* fn = dynamic_cast<const FunctionDecl*>(d.get());
        if (!fn || !fn->body) continue;
        // Fresh maps per body: a local from another function has no say
        // here.  Global barriers come from `env`, not from `bound`.
        std::map<std::string, int> bound, carried;
        for (const auto& g : env.globals) bound[g.first] = g.second;
        bad += walkBody(fn->body.get(), fn->name, env, bound, carried);
    }
    return bad;
}

}  // namespace clearance
}  // namespace aiml
