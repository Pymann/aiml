#include "../../../logger.h"
#include "../ast_builder.h"
#include "statement_builder.h"
#include "expression_builder.h"
#include <cstdlib>   // std::getenv — spec/61 §10 memV3 gate for materializing `free:`

namespace AIML {
namespace AST {

bool StatementBuilder::OnSequenceStart() {
    seqDepth++;
    // Detect inner sequence in `decl: [name, buffer<T>, [v1, v2, ...]]`
    if (seqDepth == 2 && type == "decl" && (int)args.size() == 2)
        innerSeqStart = (int)args.size(); // always 2, but keep it explicit
    return true;
}

bool StatementBuilder::OnSequenceEnd() {
    seqDepth--;
    if (seqDepth <= 0)
        builder->popState();
    return true;
}

bool StatementBuilder::OnMapEnd() {
    builder->popState();
    return true;
}

bool StatementBuilder::OnScalar(const std::string& value, const std::string& tag) {
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
    if (type == "call" && args.size() > 1) {
        orderedCallArgs.push_back(parseScalarExpr(value, tag));
        args.pop_back(); // keep only function name in args
        return true;
    }

    if (type == "set" && args.size() == 1) {
        // After reading `x`, the next thing might be an expression map instead of a scalar
        expectingValue = true; 
    }
    
    if (type == "return") {
        exprArg = parseScalarExpr(value, tag);
    }
    
    return true;
}

class ModifierBuilder : public StateHandler {
    bool* is_const;
    bool* is_volatile;
    bool* is_extern;
    std::string* lifetime;
    bool expectingKey = true;
    std::string currentKey;
public:
    ModifierBuilder(Builder* b, bool* ic, bool* iv, bool* ie, std::string* lt)
        : StateHandler(b), is_const(ic), is_volatile(iv), is_extern(ie), lifetime(lt) {}
    bool OnMapStart() override { expectingKey = true; return true; }
    bool OnMapEnd() override { builder->popState(); return true; }
    bool OnScalar(const std::string& value, const std::string& tag) override {
        if (expectingKey) {
            currentKey = value;
            expectingKey = false;
        } else {
            if (currentKey == "const")    *is_const   = (value == "true" || value == "1");
            if (currentKey == "volatile") *is_volatile = (value == "true" || value == "1");
            if (currentKey == "extern")   *is_extern   = (value == "true" || value == "1");
            if (currentKey == "lifetime") {  // spec/61 ownership qualifier
                if (value == "scope" || value == "persistent" || value == "consumed") {
                    if (lifetime) *lifetime = value;
                } else {
                    LOG_ERROR("decl lifetime must be scope|persistent|consumed, got '" + value + "'");
                }
            }
            expectingKey = true;
        }
        return true;
    }
    std::unique_ptr<Node> finish() override { return nullptr; }
};

bool StatementBuilder::OnMapStart() {
    if (type == "decl" && args.size() >= 2) {
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
            builder->pushState(std::make_unique<ModifierBuilder>(builder, &is_const, &is_volatile, &is_extern, &decl_lifetime));
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
        if (type == "call") {
            orderedCallArgs.push_back(std::unique_ptr<Expr>(e));
        } else {
            exprArg = std::unique_ptr<Expr>(e);
        }
    }
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
                         if (k != "const" && k != "volatile" && k != "extern" && k != "lifetime") {
                             modifierOnly = false;
                             break;
                         }
                     }
                     if (modifierOnly) {
                         // spec/61 §2: the modifier map (incl. `lifetime`) is the 4th element,
                         // after the initializer. For a qualified local with no init, put the
                         // `null` literal in the 3rd (init) slot: [name, type, null, { lifetime: persistent }].
                         if (capturedLine > 0)
                             LOG_ERROR("line " + std::to_string(capturedLine) + ": decl modifiers must be the 4th list element: [name, type, value, { const: true }] (use null as the value when there is no initializer, e.g. [buf, T, null, { lifetime: persistent }]). A modifier map in 3rd position is invalid.");
                         else
                             LOG_ERROR("decl modifiers must be the 4th list element: [name, type, value, { const: true }] (use null as the value when there is no initializer). A modifier map in 3rd position is invalid.");
                         return nullptr;
                     }
                 }
             }
             decl->initialValue = std::move(exprArg);
         } else if (innerSeqStart >= 0
                    && typeName.rfind("buffer<", 0) == 0
                    && typeName.back() == '>') {
             // Buffer literal: decl: [name, buffer<T>, [v1, v2, ...]]
             // Expand into a BufferLiteralExpr — visitDeclStmt emits push calls.
             auto lit = std::make_unique<BufferLiteralExpr>();
             for (int i = innerSeqStart; i < (int)args.size(); ++i)
                 lit->elements.push_back(parseScalarExpr(args[i].first, args[i].second));
             decl->initialValue = std::move(lit);
         } else if (initStart < (int)args.size()) {
             decl->initialValue = parseScalarExpr(args[initStart].first, args[initStart].second);
         }

         decl->is_const    = is_const;
         decl->is_volatile = is_volatile;
         decl->is_extern   = is_extern;
         decl->lifetime    = decl_lifetime;
         decl->sourceLine  = capturedLine;
         return decl;
    }
    if (type == "set" && args.size() >= 1) {
         // Häufiger Bug-Magnet: `set: [list, idx, value]` als Index-Schreibe.
         // set: hat NUR target + value (2 Args).  Index-Mutation ist `idx:`.
         // 3+ scalar args sind also IMMER falsch — der Wert würde silent
         // verschluckt und der Index nie geschrieben → Runtime-Bug ohne
         // Compile-Hinweis.  Wir bricht hier hart ab und schlägt `idx:` vor.
         if (args.size() >= 3 && !exprArg) {
             LOG_ERROR("line " + std::to_string(capturedLine)
                       + ": set: nimmt nur [target, value] — fuer Index-Schreiben benutze idx: [container, idx, value]."
                       + " Got " + std::to_string(args.size()) + " scalar args; das Erste ist target='"
                       + args[0].first + "', der dritte Wert wuerde silent ignoriert.");
         }
         if (!exprArg && args.size() >= 2) {
             const std::string& val = args[1].first;
             const std::string& tag = args[1].second;
             exprArg = parseScalarExpr(val, tag);
         }
         auto set = std::make_unique<SetStmt>(args[0].first, std::move(exprArg));
         set->sourceLine = capturedLine;
         return set;
    }
    // store: [ptr_var, value, (optional)depth]
    if (type == "store" && args.size() >= 1) {
        if (args.size() < 2 && !exprArg) {
            LOG_ERROR("line " + std::to_string(capturedLine)
                      + ": store: braucht mindestens [ptr_var, value]");
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
                    LOG_ERROR("line " + std::to_string(capturedLine)
                              + ": store: depth muss eine Integer-Konstante sein, got '"
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
                    LOG_ERROR("line " + std::to_string(capturedLine)
                              + ": store: depth muss eine Integer-Konstante sein, got '"
                              + args[2].first + "'");
                    return nullptr;
                }
            }
        }
        if (depth < 1) {
            LOG_ERROR("line " + std::to_string(capturedLine)
                      + ": store: depth >= 1 erforderlich, got " + std::to_string(depth));
            return nullptr;
        }
        auto st = std::make_unique<StoreStmt>(args[0].first, std::move(val), depth);
        st->sourceLine = capturedLine;
        return st;
    }
    // idx: [container, idx1, ..., idxN, value]
    // Minimum: container + 1 index + value.  Value can be a scalar arg OR an
    // expression map captured in exprArg — the latter means args.size() is 2
    // (container + index).  Without this fallback the whole idx statement was
    // silently dropped when the value was an inline expression like
    // `idx: [list, i, { call: [...] }]`.
    if (type == "idx" && (args.size() >= 3 || (args.size() >= 2 && exprArg))) {
         auto idx = std::make_unique<IdxStmt>(args[0].first);
         // Middle args are indices (as expressions), last arg OR exprArg is the value
         size_t lastScalar = args.size() - 1;
         if (exprArg) {
             // Value was an expression map: idx: [arr, i, { "+": [a, b] }]
             for (size_t i = 1; i < args.size(); ++i)
                 idx->indices.push_back(parseScalarExpr(args[i].first, args[i].second));
             idx->value = std::move(exprArg);
         } else {
             // Value is last scalar: idx: [arr, i, 42.0]
             for (size_t i = 1; i < lastScalar; ++i)
                 idx->indices.push_back(parseScalarExpr(args[i].first, args[i].second));
             idx->value = parseScalarExpr(args[lastScalar].first, args[lastScalar].second);
         }
         idx->sourceLine = capturedLine;
         return idx;
    }
    if (type == "call" && args.size() >= 1) {
         auto call = std::make_unique<CallExpr>(args[0].first);
         // orderedCallArgs preserves YAML source order (scalars and exprs interleaved)
         for (auto& ea : orderedCallArgs) call->args.push_back(std::move(ea));
         call->sourceLine = capturedLine;
         auto exprStmt = std::make_unique<ExprStmt>(std::move(call));
         exprStmt->sourceLine = capturedLine;
         return exprStmt;
    }
    if (type == "print" && args.size() >= 1) {
         auto call = std::make_unique<CallExpr>("print");
         for (size_t i = 0; i < args.size(); ++i) {
             const std::string& val = args[i].first;
             const std::string& tag = args[i].second;
             call->args.push_back(parseScalarExpr(val, tag));
         }
         call->sourceLine = capturedLine;
         auto exprStmt = std::make_unique<ExprStmt>(std::move(call));
         exprStmt->sourceLine = capturedLine;
         return exprStmt;
    }
    if (type == "return") {
         auto ret = std::make_unique<RetStmt>();
         if (exprArg) ret->value = std::move(exprArg);
         ret->sourceLine = capturedLine;
         return ret;
    }
    // spec/61 §6.1/§10 (memV3 only): materialize `free: [p]` as an ExprStmt(CallExpr("free", …))
    // so the explicit release is a real AST node — enabling the §10 ownership diagnostics
    // (use-after-free / double-free, borrow_check.cpp) AND an active early-release in codegen.
    // Under OFF this key is left to drop to nullptr exactly as before (byte-identical: `free:`
    // has historically been a no-op, with scope-drop doing the release). Gated by the same env
    // read as CompilerContext::memV3 so parse-time and compile-time agree.
    if (type == "free") {
        static const bool kMemV3 = [](){
            const char* mv = std::getenv("AIML_MEM_V3");
            if (!mv) return false; std::string s = mv;
            return !(s.empty() || s == "0" || s == "off" || s == "OFF" || s == "false" || s == "FALSE");
        }();
        if (kMemV3 && (args.size() >= 1 || !orderedCallArgs.empty())) {
            auto call = std::make_unique<CallExpr>("free");
            for (auto& ea : orderedCallArgs) call->args.push_back(std::move(ea));
            for (size_t i = 0; i < args.size(); ++i)
                call->args.push_back(parseScalarExpr(args[i].first, args[i].second));
            call->sourceLine = capturedLine;
            auto exprStmt = std::make_unique<ExprStmt>(std::move(call));
            exprStmt->sourceLine = capturedLine;
            return exprStmt;
        }
        // OFF: fall through to the historical nullptr drop.
    }
    if (type == "throw") {
         if (exprArg) {
             auto t = std::make_unique<ThrowStmt>(std::move(exprArg));
             t->sourceLine = capturedLine;
             return t;
         } else {
             LOG_ERROR("throw statement must have a value");
             return nullptr;
         }
    }
    // Fallthrough: some valid statements (e.g. builtin `exit:`) are handled on
    // a later path and legitimately return nullptr here, so we do NOT hard-error
    // on an unknown key — that produced false positives.  The bare-scalar case
    // (the actual footgun) is caught earlier in BlockBuilder::OnScalar.
    return nullptr;
}

} // namespace AST
} // namespace AIML
