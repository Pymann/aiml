#include "../clearance.h"
#include "../../../logger.h"
#include "../ast_builder.h"
#include "statement_builder.h"
#include <map>
#include <set>
#include "expression_builder.h"
#include <cstdlib>   // std::getenv — spec/61 §10 memV3 gate for materializing `free:`

namespace AIML {
namespace AST {

bool StatementBuilder::OnSequenceStart() {
    seqDepth++;
    // Detect inner sequence in `decl: [name, buffer<T>, [v1, v2, ...]]`
    if (seqDepth == 2 && type == "decl" && (int)args.size() == 2)
        innerSeqStart = (int)args.size(); // always 2, but keep it explicit
    // spec/10: dasselbe Literal auf der rechten Seite von `set: [ziel, [v1, v2, ...]]`.
    // `expectingValue` muss hier WEG, sonst schluckt OnScalar das erste Element als
    // skalaren Wert und der Rest fiel still weg (SIGSEGV, Backlog 13/36).
    if (seqDepth == 2 && type == "set" && (int)args.size() == 1) {
        innerSeqStart = (int)args.size();
        expectingValue = false;
    }
    // spec/10: `call: [f, [v1, v2]]` — die innere Sequenz ist EIN Listenargument.
    if (seqDepth == 2 && type == "call" && !pendingCallLiteral)
        pendingCallLiteral = std::make_unique<BufferLiteralExpr>();
    // spec/10: `return: [v1, v2]` ist ein LISTENLITERAL.  Ohne diese Fahne landeten
    // die Elemente in `args`, `finish()` baute daraus keinen Wert, und die Funktion
    // gab einen undefinierten Rueckgabewert zurueck: `len` war Muell und der erste
    // Zugriff stuerzte ab (gemessen 20.11.2026 im Korpus-Erzeuger).
    if (seqDepth == 1 && type == "return")
        retSeq = true;
    return true;
}

bool StatementBuilder::OnSequenceEnd() {
    seqDepth--;
    if (seqDepth == 1 && pendingCallLiteral) {
        orderedCallArgs.push_back(std::move(pendingCallLiteral));
        pendingCallLiteral.reset();
    }
    if (seqDepth <= 0)
        builder->popState();
    return true;
}

bool StatementBuilder::OnMapEnd() {
    builder->popState();
    return true;
}

bool StatementBuilder::OnScalar(const std::string& value, const std::string& tag) {
    scalarCount++;
    if (expectingValue) {
        // We received the RHS of a `set: [x, value]`
        exprArg = parseScalarExpr(value, tag);
        expectingValue = false;
        return true;
    }
    
    // Collecting arguments inside the sequence `[x, i32, 5]`
    args.push_back({value, tag});

    // For call statements: redirect args after the function name to the ordered list
    // so that scalar args and nested-expr args are interleaved in YAML source order.
    if (pendingCallLiteral) {
        pendingCallLiteral->elements.push_back(parseScalarExpr(value, tag));
        args.pop_back();
        return true;
    }
    if (type == "call" && args.size() > 1) {
        orderedCallArgs.push_back(parseScalarExpr(value, tag));
        args.pop_back(); // keep only function name in args
        return true;
    }

    // print: scalars and expression maps keep their source order (spec/09 §print).
    if (type == "print") {
        orderedCallArgs.push_back(parseScalarExpr(value, tag));
        args.pop_back();
        return true;
    }

    if (type == "idx" && args.size() > 1) {
        if (!value.empty() && value[0] == '.') idxParts.push_back({value.substr(1), nullptr});
        else idxParts.push_back({"", parseScalarExpr(value, tag)});
        args.pop_back();   // keep only the container in args
        return true;
    }

    if (type == "set" && args.size() == 1) {
        // After reading `x`, the next thing might be an expression map instead of a scalar
        expectingValue = true; 
    }
    
    if (type == "return") {
        // `return: [a, b]` -- die Elemente gehoeren ins Listenliteral (args), NICHT
        // einzeln in exprArg: dort gewann das letzte, und eine `list<i32>`-Funktion
        // gab `ret i32 9` zurueck (gemessen 21.11.2026).
        if (!retSeq) exprArg = parseScalarExpr(value, tag);
    }
    
    return true;
}

class ModifierBuilder : public StateHandler {
    bool* is_const;
    bool* is_singleton;      // spec/21 §6.2 F21-S1
    bool* is_volatile;
    bool* is_extern;
    std::string* lifetime;
    int* level;                 // spec/65 F65-19: `{ level: N }` an 4. Position
    std::string where;          // Fundstelle fuer die Diagnose
    bool expectingKey = true;
    std::string currentKey;
public:
    ModifierBuilder(Builder* b, bool* ic, bool* iv, bool* ie, std::string* lt,
                    int* lv, std::string wh, bool* sg = nullptr)
        : StateHandler(b), is_const(ic), is_singleton(sg), is_volatile(iv), is_extern(ie),
          lifetime(lt), level(lv), where(std::move(wh)) {}
    bool OnMapStart() override { expectingKey = true; return true; }
    bool OnMapEnd() override { builder->popState(); return true; }
    bool OnScalar(const std::string& value, const std::string& tag) override {
        if (expectingKey) {
            currentKey = value;
            expectingKey = false;
        } else {
            if (currentKey == "const")    *is_const   = (value == "true" || value == "1");
            if (currentKey == "singleton" && is_singleton)
                *is_singleton = (value == "true" || value == "1");
            if (currentKey == "volatile") *is_volatile = (value == "true" || value == "1");
            if (currentKey == "extern")   *is_extern   = (value == "true" || value == "1");
            if (currentKey == "lifetime") {  // spec/61 ownership qualifier
                if (value == "scope" || value == "persistent" || value == "consumed") {
                    if (lifetime) *lifetime = value;
                } else {
                    LOG_ERROR(builder->fmtLoc() + "decl lifetime must be scope|persistent|consumed, got '" + value + "' (spec/61 §9)");
                }
            }
            if (currentKey == "level") {     // spec/65: pruefen, nicht durchsetzen
                int parsed = aiml::clearance::kAbsent;
                if (aiml::clearance::parseLevel(value, where, parsed) && level)
                    *level = parsed;
            }
            expectingKey = true;
        }
        return true;
    }
    std::unique_ptr<Node> finish() override { return nullptr; }
};

bool StatementBuilder::OnMapStart() {
    // spec/10: innerhalb der inneren Sequenz eines Listen-/Buffer-Literals ist eine Map ein
    // ELEMENT (`["P", { call: ... }]`), niemals ein Modifier-Block.  Ohne diese Abfrage
    // schickte `args.size() >= 3` die Element-Map in den ModifierBuilder, der unbekannte
    // Schluessel still verwirft — das Element fiel weg, ohne Warnung.
    if (type == "decl" && args.size() >= 2 && !insideSeqLiteral()) {
        // If args size is 3, we already have Name, Type, Value. This map is Modifiers.
        // If args size is 2, it could be Value (an expr) or Modifiers.
        // We'll assume for simplicity that in AIML local var decl: [name, type, val, {modifiers}]
        // OR [name, type, {modifiers}]. Wait, we can look at `expectingValue`.
        // Actually, if we just push ModifierBuilder when args.size() >= 2, we might break default exprs.
        // Let's only push ModifierBuilder if args.size() >= 3. Wait, test_modifiers.aiml does NOT omit default value.
        // So args.size() is 3.
        // Modifier map when the value slot is already filled: either a scalar value made
        // args.size()>=3, OR a MAP value was already captured into exprArg (a map value does not
        // grow `args`, so a following map is the modifier). Fixes spec/61
        // `[name, type, {new:T}, {lifetime: persistent}]`.
        if (args.size() >= 3 || exprArg) {
            builder->pushState(std::make_unique<ModifierBuilder>(builder, &is_const, &is_volatile, &is_extern, &decl_lifetime, &decl_level,
                                   "decl " + (args.empty() ? std::string("(?)") : args[0].first), &is_singleton));
            return true;
        }
    }
    // A statement received a map, likely an expression like `set: [x, { add: [1, 2] }]`
    expectingValue = false;
    auto exprBuilder = std::make_unique<ExpressionBuilder>(builder);
    exprBuilder->OnMapStart();
    builder->pushState(std::move(exprBuilder));
    return true;
}

void StatementBuilder::receiveChildNode(std::unique_ptr<Node> node) {
    if (auto e = dynamic_cast<Expr*>(node.release())) {
        if (type == "call" || type == "print") {
            orderedCallArgs.push_back(std::unique_ptr<Expr>(e));
        } else if (type == "idx") {
            idxParts.push_back({"", std::unique_ptr<Expr>(e)});
        } else if (insideSeqLiteral()) {
            // Element eines Listen-/Buffer-Literals: mit seiner Stelle festhalten.  In
            // `exprArg` waere es verloren (dort steht der Initialwert des decl) — stand die
            // Map VORN, nahm finish() sie als ganzen Initialwert und schrieb einen String in
            // den Listen-Slot: `.len` Muell, Heap beschaedigt.
            innerSeqExprs.emplace_back((int)args.size(), std::unique_ptr<Expr>(e));
        } else {
            exprArg = std::unique_ptr<Expr>(e);
        }
    }
}

std::unique_ptr<BufferLiteralExpr> StatementBuilder::buildSeqLiteral(int from) {
    auto lit = std::make_unique<BufferLiteralExpr>();
    size_t nextExpr = 0;
    for (int i = from; i <= (int)args.size(); ++i) {
        // Alle Expressions, die VOR dem Skalar args[i] eintrafen (ihre Stelle ist <= i).
        while (nextExpr < innerSeqExprs.size() && innerSeqExprs[nextExpr].first <= i)
            lit->elements.push_back(std::move(innerSeqExprs[nextExpr++].second));
        if (i < (int)args.size())
            lit->elements.push_back(parseScalarExpr(args[i].first, args[i].second));
    }
    innerSeqExprs.clear();
    return lit;
}

std::unique_ptr<Node> StatementBuilder::finish() {
    if (type == "decl" && args.size() >= 2) {
         // Rejoin type-name fragments split by YAML commas inside angle brackets.
         // e.g. args=["fp", "fn<i32", "i32:i32>"] → typeName="fn<i32, i32:i32>", initStart=2
         std::string typeName = args[1].first;
         int typeEndIdx = 1;
         {
             int depth = 0;
             for (char c : typeName) { if (c=='<') ++depth; else if (c=='>') --depth; }
             while (depth > 0 && typeEndIdx + 1 < (int)args.size()) {
                 ++typeEndIdx;
                 typeName += ", " + args[typeEndIdx].first;
                 for (char c : args[typeEndIdx].first) { if (c=='<') ++depth; else if (c=='>') --depth; }
             }
         }
         int initStart = typeEndIdx + 1; // index of first init-value arg

         auto decl = std::make_unique<DeclStmt>(args[0].first, typeName);
         if (exprArg) {
             if (initStart == 2) {
                 if (auto* dict = dynamic_cast<DictLiteralExpr*>(exprArg.get())) {
                     bool modifierOnly = !dict->entries.empty();
                     for (const auto& [k, v] : dict->entries) {
                         if (k != "const" && k != "volatile" && k != "extern" && k != "lifetime" &&
                             k != "level" && k != "singleton") {
                             modifierOnly = false;
                             break;
                         }
                     }
                     if (modifierOnly) {
                         // spec/61 §2: the modifier map (incl. `lifetime`) is the 4th element,
                         // after the initializer. For a qualified local with no init, put the
                         // `null` literal in the 3rd (init) slot: [name, type, null, { lifetime: persistent }].
                         if (capturedLine > 0)
                             LOG_ERROR(builder->fmtLoc(capturedLine, capturedColumn) + "decl modifiers must be the 4th list element: [name, type, value, { const: true }] (use null as the value when there is no initializer, e.g. [buf, T, null, { lifetime: persistent }]). A modifier map in 3rd position is invalid.");
                         else
                             LOG_ERROR("decl modifiers must be the 4th list element: [name, type, value, { const: true }] (use null as the value when there is no initializer). A modifier map in 3rd position is invalid.");
                         return nullptr;
                     }
                 }
             }
             decl->initialValue = std::move(exprArg);
         } else if (innerSeqStart >= 0
                    && (typeName.rfind("buffer<", 0) == 0 || typeName.rfind("list<", 0) == 0)
                    && typeName.back() == '>') {
             // Buffer/list literal: decl: [name, buffer<T>|list<T>, [v1, v2, ...]] (spec/10, spec/11)
             // Expand into a BufferLiteralExpr — visitDeclStmt emits write/push calls.
             decl->initialValue = buildSeqLiteral(innerSeqStart);
         } else if (initStart < (int)args.size()) {
             decl->initialValue = parseScalarExpr(args[initStart].first, args[initStart].second);
         }

         decl->is_const    = is_const;
         decl->is_singleton = is_singleton;
         decl->is_volatile = is_volatile;
         decl->is_extern   = is_extern;
         decl->lifetime    = decl_lifetime;
         decl->level       = decl_level;
         decl->sourceLine  = capturedLine;
        decl->sourceColumn  = capturedColumn;
         return decl;
    }
    if (type == "set" && args.size() >= 1) {
         // Häufiger Bug-Magnet: `set: [list, idx, value]` als Index-Schreibe.
         // set: hat NUR target + value (2 Args).  Index-Mutation ist `idx:`.
         // 3+ scalar args sind also IMMER falsch — der Wert würde silent
         // verschluckt und der Index nie geschrieben → Runtime-Bug ohne
         // Compile-Hinweis.  Wir bricht hier hart ab und schlägt `idx:` vor.
         // spec/02: GENAU [ziel, wert].  Gezaehlt werden die echten Skalare — `args.size()`
         // war hier blind (expectingValue nimmt den zweiten Skalar vorweg), weshalb diese
         // Pruefung nie ausloeste und das dritte Element still verschluckt wurde.
         if (scalarCount >= 3 && innerSeqStart < 0) {
             LOG_ERROR(builder->fmtLoc(capturedLine, capturedColumn) + "set: nimmt GENAU [target, value] — fuer Index-Schreiben benutze"
                       + " idx: [container, index, value] (spec/10), fuer den ganzen Inhalt"
                       + " set: [target, [v1, v2, ...]] (spec/10). Gesehen: "
                       + std::to_string(scalarCount) + " Skalare, target='" + args[0].first
                       + "'; alles ab dem dritten wuerde stillschweigend verschwinden.");
             return nullptr;
         }
         if (innerSeqStart >= 0) {
             // spec/10: Listen-/Buffer-Literal als Wert — visitSetStmt loest es in
             // `clear` + ein `push` je Element auf, genau wie am decl.
             exprArg = buildSeqLiteral(innerSeqStart);
         } else if (!exprArg && args.size() >= 2) {
             const std::string& val = args[1].first;
             const std::string& tag = args[1].second;
             exprArg = parseScalarExpr(val, tag);
         }
         auto set = std::make_unique<SetStmt>(args[0].first, std::move(exprArg));
         set->sourceLine = capturedLine;
        set->sourceColumn = capturedColumn;
         return set;
    }
    // store: [ptr_var, value, (optional)depth]
    if (type == "store" && args.size() >= 1) {
        if (args.size() < 2 && !exprArg) {
            LOG_ERROR(builder->fmtLoc(capturedLine, capturedColumn) + "store: braucht mindestens [ptr_var, value]");
            return nullptr;
        }
        std::unique_ptr<Expr> val;
        int depth = 1;
        if (exprArg) {
            // store: [p, { '+': [a, b] }]  — value ist eine Map (exprArg).
            // Optionaler depth landet dann als 2. scalar in args.
            val = std::move(exprArg);
            if (args.size() >= 2) {
                try { depth = std::stoi(args[1].first); }
                catch (...) {
                    LOG_ERROR(builder->fmtLoc(capturedLine, capturedColumn) + "store: depth muss eine Integer-Konstante sein, got '"
                              + args[1].first + "'");
                    return nullptr;
                }
            }
        } else {
            // store: [p, value, (depth)]  — alles Scalars.
            val = parseScalarExpr(args[1].first, args[1].second);
            if (args.size() >= 3) {
                try { depth = std::stoi(args[2].first); }
                catch (...) {
                    LOG_ERROR(builder->fmtLoc(capturedLine, capturedColumn) + "store: depth muss eine Integer-Konstante sein, got '"
                              + args[2].first + "'");
                    return nullptr;
                }
            }
        }
        if (depth < 1) {
            LOG_ERROR(builder->fmtLoc(capturedLine, capturedColumn) + "store: depth >= 1 erforderlich, got " + std::to_string(depth));
            return nullptr;
        }
        auto st = std::make_unique<StoreStmt>(args[0].first, std::move(val), depth);
        st->sourceLine = capturedLine;
        st->sourceColumn = capturedColumn;
        return st;
    }
    // idx: [container, idx1, ..., idxN, value]
    // Minimum: container + 1 index + value.  Value can be a scalar arg OR an
    // expression map captured in exprArg — the latter means args.size() is 2
    // (container + index).  Without this fallback the whole idx statement was
    // silently dropped when the value was an inline expression like
    // `idx: [list, i, { call: [...] }]`.
    // Parts after the container keep their source order, so index and value may each be a scalar
    // or an expression map: idx: [arr, { -: [i, 1] }, 42.0].
    if (type == "idx" && args.size() >= 1 && idxParts.size() >= 2) {
         auto idx = std::make_unique<IdxStmt>(args[0].first);
         // Middle parts are INDICES, then optional DOT-PREFIXED member names (.field), then the value.
         // idx: [container, i, .field, value] writes element[i].field = value (dash-free element-set).
         for (size_t i = 0; i + 1 < idxParts.size(); ++i) {
             if (idxParts[i].second) idx->indices.push_back(std::move(idxParts[i].second));
             else idx->memberPath.push_back(idxParts[i].first);
         }
         if (!idxParts.back().second) {
             LOG_ERROR(builder->fmtLoc(capturedLine, capturedColumn) + "idx: the last element must be the value");
             return nullptr;
         }
         idx->value = std::move(idxParts.back().second);
         idx->sourceLine = capturedLine;
        idx->sourceColumn = capturedColumn;
         return idx;
    }
    if (type == "call" && args.size() >= 1) {
         auto call = std::make_unique<CallExpr>(args[0].first);
         // orderedCallArgs preserves YAML source order (scalars and exprs interleaved)
         for (auto& ea : orderedCallArgs) call->args.push_back(std::move(ea));
         call->sourceLine = capturedLine;
        call->sourceColumn = capturedColumn;
         auto exprStmt = std::make_unique<ExprStmt>(std::move(call));
         exprStmt->sourceLine = capturedLine;
        exprStmt->sourceColumn = capturedColumn;
         return exprStmt;
    }
    if (type == "print" && !orderedCallArgs.empty()) {
         auto call = std::make_unique<CallExpr>("print");
         for (auto& ea : orderedCallArgs) call->args.push_back(std::move(ea));
         call->sourceLine = capturedLine;
        call->sourceColumn = capturedColumn;
         auto exprStmt = std::make_unique<ExprStmt>(std::move(call));
         exprStmt->sourceLine = capturedLine;
        exprStmt->sourceColumn = capturedColumn;
         return exprStmt;
    }
    if (type == "return") {
         auto ret = std::make_unique<RetStmt>();
         if (exprArg) ret->value = std::move(exprArg);
         // `return: [a, b]` / `return: []` -- die Elemente stehen in args/innerSeqExprs.
         // Den Elementtyp bestimmt der erklaerte Rueckgabetyp (visitRetStmt setzt
         // listLiteralArgType), darum traegt auch die leere Liste.
         else if (retSeq) ret->value = buildSeqLiteral(0);
         ret->sourceLine = capturedLine;
        ret->sourceColumn = capturedColumn;
         return ret;
    }
    // spec/61 §6.1/§10 (memV3 only): materialize `free: [p]` as an ExprStmt(CallExpr("free", …))
    // so the explicit release is a real AST node — enabling the §10 ownership diagnostics
    // (use-after-free / double-free, borrow_check.cpp) AND an active early-release in codegen.
    // With AIML_MEM_V3=0/off this key is left to drop to nullptr exactly as before (byte-identical:
    // `free:` has historically been a no-op, with scope-drop doing the release). Gated by the SAME
    // env read + default as CompilerContext::memV3 (context.h: default ON when unset) so parse-time
    // and compile-time agree — a mismatch here silently makes `free:` a no-op while codegen believes
    // memV3 is on, leaking every `free`d box.
    if (type == "free") {
        static const bool kMemV3 = [](){
            const char* mv = std::getenv("AIML_MEM_V3");
            if (!mv) return true; std::string s = mv;   // default ON, matching CompilerContext::memV3
            return !(s.empty() || s == "0" || s == "off" || s == "OFF" || s == "false" || s == "FALSE");
        }();
        if (kMemV3 && (args.size() >= 1 || !orderedCallArgs.empty())) {
            auto call = std::make_unique<CallExpr>("free");
            for (auto& ea : orderedCallArgs) call->args.push_back(std::move(ea));
            for (size_t i = 0; i < args.size(); ++i)
                call->args.push_back(parseScalarExpr(args[i].first, args[i].second));
            call->sourceLine = capturedLine;
        call->sourceColumn = capturedColumn;
            auto exprStmt = std::make_unique<ExprStmt>(std::move(call));
            exprStmt->sourceLine = capturedLine;
        exprStmt->sourceColumn = capturedColumn;
            return exprStmt;
        }
        // OFF: fall through to the historical nullptr drop.
    }
    if (type == "throw") {
         if (!exprArg && !args.empty())   // `throw: err` — rethrow / throw an existing object (spec/22)
             exprArg = parseScalarExpr(args[0].first, args[0].second);
         if (exprArg) {
             auto t = std::make_unique<ThrowStmt>(std::move(exprArg));
             t->sourceLine = capturedLine;
        t->sourceColumn = capturedColumn;
             return t;
         } else {
             LOG_ERROR(builder->fmtLoc(capturedLine, capturedColumn) + "throw statement must have a value (spec/22)");
             return nullptr;
         }
    }
    // spec/49 §2a: an unknown statement key is an ERROR, not a no-op.
    //
    // Until 2026-10-15 this fell through to a silent `return nullptr`, and
    // BlockBuilder dropped the result without a word — the statement simply
    // vanished.  The comment here claimed `exit:` was "handled on a later
    // path"; it was not.  `- exit: [1]` did nothing, in 54 places in the test
    // corpus, so every FAIL branch returned 0 and the suite reported PASS.
    // `- continue: []` (which aiml does not have) vanished the same way.
    //
    // A key that reaches this point with too FEW arguments was already
    // diagnosed above, so everything left here is an unknown key.
    {
        static const std::map<std::string, std::string> kHints = {
            {"clearance", "clearance-self-certify: die Freigabe setzt der WIRT, nicht die Quelle (spec/65 F65-6)"},
            {"exit",     "use `call: [sys.exit, N]` (spec/23)"},
            {"continue", "aiml has no `continue`; restructure the loop body with `if:`"},
            {"break",    "aiml has no `break`; use the loop condition (spec/06)"},
            {"for",      "aiml has `while:` (spec/06)"},
            {"else",     "`else:` belongs INSIDE `if:`, next to `then:` (spec/05)"},
            {"log",      "use `call: [log.info, ...]` (spec/46)"},
            {"assert",   "use `if:` with a `throw:` or `call: [sys.exit, N]`"},
            {"let",      "use `decl: [name, type, value]`"},
            {"var",      "use `decl: [name, type, value]`"},
            {"spawn",    "`spawn:` is an expression: `decl: [h, thread_handle, { spawn: [fn, args] }]` (spec/26)"},
        };
        // Ein BEKANNTER Schluessel, der hier landet, ist nicht unbekannt, sondern
        // unvollstaendig (`decl:` mit einem Argument, `call:` ohne Namen, …).
        // Beides zu vermengen schickt den Leser in die falsche Richtung.
        static const std::set<std::string> kKnown = {
            "decl", "set", "store", "idx", "call", "print", "return", "throw"
        };
        std::string msg;
        if (kKnown.count(type)) {
            msg = "malformed '" + type + "' statement: not enough arguments to build it.";
        } else if (type == "free") {
            msg = "malformed 'free' statement: expected `free: [p]` (spec/20).";
        } else {
            msg = "unknown statement '" + type + "' — not an aiml statement.";
            auto hint = kHints.find(type);
            if (hint != kHints.end()) msg += "  " + hint->second + ".";
            else msg += "  Statements are: decl, set, store, idx, call, print, return, free,"
                        " throw, if, while, ret, try, text.";
        }
        msg = builder->fmtLoc(capturedLine, capturedColumn) + msg;
        LOG_ERROR(msg);
    }
    return nullptr;
}

} // namespace AST
} // namespace AIML
