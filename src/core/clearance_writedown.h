#pragma once
// spec/65 F65-34 — die Write-down-Regel, OPT-IN wie F65-17 (derselbe Hebel wie die
// Flusspruefung: ein Schalter, nicht zwei).
//
// Ein Wert von Level L darf nicht in einen Ort geschrieben werden, der WENIGER
// eingeschraenkt ist als L.  Sonst ist ein eingeschraenkter Wert in einer Zuweisung
// gewaschen: dort lesen, wo man darf, dort ablegen, wo jeder darf.
//
// Zwei Bedeutungen je Lokale, die auseinandergehalten werden muessen:
//   `level:`  = die SCHRANKE fuer das Speichern — was darf hier hinein.
//   getragen  = das Strengste, was je hineinlief — und das reist weiter.
// Darum traegt auch eine Lokale OHNE Annotation ein Level, sobald ein eingeschraenkter
// Wert hineinlaeuft; ohne das waere die Regel mit `tmp = read_secret()` umgangen.
//
// Ohne Annotation UND ohne eingeschraenkte Herkunft traegt eine Lokale kein Level — nicht
// Level 0.  Das ist der Unterschied zu F65-5: dort ist 0 die fail-closed-Vorgabe fuer den
// ZUGRIFF; hier waere jedes Literal in einer annotierten Lokalen ein Verstoss, und eine
// Regel, die jedes Programm bricht, schuetzt nichts.
#include <map>
#include <string>
#include "ast_nodes.h"
#include "clearance.h"
#include "../../logger.h"

namespace aiml {
namespace clearance {

using namespace AIML::AST;

// Was eine Datei an Leveln kennt: eigene Funktionen und Globale.  Datei-UEBERGREIFEND
// deckt das nichts ab — dieselbe Grenze wie bei der Flusspruefung, und aus demselben
// Grund: eine importierte Deklaration traegt kein Level.
struct LevelEnv {
    std::map<std::string, int> funcs;
    std::map<std::string, int> globals;
};

// Das Strengste (= kleinste) annotierte Level, das in diesen Ausdruck einflieszt.
// kAbsent heiszt: keine eingeschraenkte Herkunft.
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

// Ein Rumpf, ein Vorwaertsdurchlauf.  `bound` sind die Schranken (`level:` an der decl),
// `carried` ist, was eine Lokale traegt.  Gibt die Zahl der Verstoesse zurueck.
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
            LOG_ERROR("clearance-writedown: in `" + fname + "` laeuft ein Wert von Level " +
                      std::to_string(from) + " in `" + dst + "`, das auf Level " +
                      std::to_string(b->second) + " deklariert ist — also in einen WENIGER "
                      "eingeschraenkten Ort (spec/65 F65-34).  Entweder `" + dst +
                      "` auf Level " + std::to_string(from) + " oder strenger deklarieren, "
                      "oder den Wert hier nicht ablegen.");
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

// Einstieg: eine Datei, ein Durchlauf.  Laeuft nur auf ausdruecklichen Wunsch (F65-17) und
// an derselben Stelle wie die Flusspruefung, damit es EINEN Hebel gibt und nicht zwei.
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
        // Je Rumpf frische Karten: eine Lokale aus einer anderen Funktion hat hier nichts
        // zu sagen.  Globale Schranken kommen aus `env`, nicht aus `bound`.
        std::map<std::string, int> bound, carried;
        for (const auto& g : env.globals) bound[g.first] = g.second;
        bad += walkBody(fn->body.get(), fn->name, env, bound, carried);
    }
    return bad;
}

}  // namespace clearance
}  // namespace aiml
