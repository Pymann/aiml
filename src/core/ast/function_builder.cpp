#include "../clearance.h"
#include "../../../logger.h"
#include "../ast_builder.h"
#include "function_builder.h"
#include "block_builder.h"

namespace AIML {
namespace AST {

class ArgsBuilder : public StateHandler {
    std::vector<std::pair<std::string, std::string>>* args_out;
    std::vector<ParamMod>* mods_out;   // parallel to args_out; one entry per parameter
    std::string currentArgName;
    std::string pendingType;  // accumulates split type names like "fn<i32"
    int typeDepth = 0;        // unbalanced '<' count in pendingType
    int depth = 1; // Start at 1 because FunctionBuilder consumed the first OnSequenceStart
    int mapDepth = 0;         // >0 while inside a per-parameter modifier map { ... }
    std::string modKey;       // current key inside a modifier map
    bool modExpectKey = true;

    // Push a completed (name, type) pair plus its default modifier slot, keeping the two
    // vectors index-synced.  The modifier map (if any) follows the pair and edits mods_out->back().
    void pushParam(const std::string& name, const std::string& type) {
        args_out->push_back({name, type});
        if (mods_out) mods_out->emplace_back();
    }
public:
    ArgsBuilder(Builder* b, std::vector<std::pair<std::string, std::string>>* out,
                std::vector<ParamMod>* mods)
        : StateHandler(b), args_out(out), mods_out(mods) {}

    bool OnSequenceStart() override {
        depth++;
        return true;
    }

    bool OnSequenceEnd() override {
        depth--;
        if (depth == 0) builder->popState();
        return true;
    }

    // spec/61 §3.1: a per-parameter modifier map `[name, type, { lifetime: consumed }]`.
    // Parameters have no init-value slot, so a map in 3rd position is unambiguously a
    // modifier (locals put it in 4th position, after the init — see statement_builder).
    bool OnMapStart() override {
        mapDepth++;
        if (mapDepth == 1) modExpectKey = true;
        return true;
    }

    bool OnMapEnd() override {
        if (mapDepth > 0) mapDepth--;
        return true;
    }

    bool OnScalar(const std::string& value, const std::string& tag) override {
        if (mapDepth > 0) {
            // Inside a parameter modifier map — parse key/value pairs into mods_out->back().
            if (modExpectKey) { modKey = value; modExpectKey = false; return true; }
            modExpectKey = true;
            if (!mods_out || mods_out->empty()) return true;  // defensive: no param to attach to
            ParamMod& pm = mods_out->back();
            if (modKey == "lifetime") {
                if (value == "scope" || value == "persistent" || value == "consumed") {
                    pm.lifetime = value;
                } else {
                    LOG_ERROR("parameter lifetime must be scope|persistent|consumed, got '" + value + "'");
                }
            } else if (modKey == "const") {
                pm.is_const = (value == "true" || value == "1");
            } else {
                LOG_ERROR("unknown parameter modifier '" + modKey + "' (expected lifetime|const)");
            }
            return true;
        }
        if (!pendingType.empty()) {
            // Continuation of a split type name (YAML comma inside angle brackets)
            pendingType += ", " + value;
            for (char c : value) { if (c=='<') ++typeDepth; else if (c=='>') --typeDepth; }
            if (typeDepth <= 0) {
                pushParam(currentArgName, pendingType);
                currentArgName = "";
                pendingType = "";
                typeDepth = 0;
            }
        } else if (currentArgName.empty()) {
            currentArgName = value;
        } else {
            // Check if the type name has unbalanced angle brackets (e.g. "fn<i32")
            int d = 0;
            for (char c : value) { if (c=='<') ++d; else if (c=='>') --d; }
            if (d > 0) {
                pendingType = value;
                typeDepth = d;
            } else {
                pushParam(currentArgName, value);
                currentArgName = "";
            }
        }
        return true;
    }
};

bool FunctionBuilder::OnMapStart() {
    mapDepth++;
    if (mapDepth > 1) {
        // spec/61 §3.2: object-form return `return: { type: T, lifetime: producer }`.
        if (currentKey == "return") {
            inReturnMap = true;
            returnMapExpectKey = true;
            return true;
        }
        // Nested map inside function definition — most likely args: { name: type }
        // which is the wrong syntax. Emit a helpful error and discard the submap.
        if (currentKey == "args") {
            LOG_ERROR("Invalid 'args' syntax in function '" + func->name + "': "
                      "use a list of pairs [[name, type], ...], not a map {name: type}. "
                      "Example: args: [[x, i32], [y, i32]]");
        }
        return true;  // stay in FunctionBuilder but depth tracks the submap
    }
    // Outer function map: normal setup
    expectingKey = true;
    return true;
}

bool FunctionBuilder::OnMapEnd() {
    mapDepth--;
    if (inReturnMap && mapDepth == 1) {   // closing the object-form `return { … }` submap
        inReturnMap = false;
        expectingKey = true;              // back to outer function-map keys
        expectingValue = false;
    }
    if (mapDepth > 0) return true;  // closing a nested submap, keep going
    builder->popState();
    return true;
}

// Simple builder that collects scalar values into a string vector.
class StringListBuilder : public StateHandler {
    std::vector<std::string>* out_;
    int depth = 1;
public:
    StringListBuilder(Builder* b, std::vector<std::string>* out)
        : StateHandler(b), out_(out) {}
    bool OnSequenceStart() override { depth++; return true; }
    bool OnSequenceEnd() override {
        depth--;
        if (depth == 0) builder->popState();
        return true;
    }
    bool OnScalar(const std::string& value, const std::string&) override {
        out_->push_back(value);
        return true;
    }
};

bool FunctionBuilder::OnSequenceStart() {
    if (currentKey == "body") {
        builder->pushState(std::make_unique<BlockBuilder>(builder));
        expectingKey = true;
        expectingValue = false;
        return true;
    } else if (currentKey == "args") {
        builder->pushState(std::make_unique<ArgsBuilder>(builder, &func->args, &func->argMods));
        expectingKey = true;
        expectingValue = false;
        return true;
    } else if (currentKey == "static_off") {
        builder->pushState(std::make_unique<StringListBuilder>(builder, &func->staticOff));
        expectingKey = true;
        expectingValue = false;
        return true;
    } else if (currentKey == "decorator" || currentKey == "decorators") {
        /* Both keys accepted — plural is standard-python; singular is what
         * the spec draft used.  Parsed identically into func->decorators. */
        builder->pushState(std::make_unique<StringListBuilder>(builder, &func->decorators));
        expectingKey = true;
        expectingValue = false;
        return true;
    } else if (currentKey == "http") {
        /* spec/66 §2 — `http: [verb, path]` on a service endpoint. */
        builder->pushState(std::make_unique<StringListBuilder>(builder, &func->http));
        expectingKey = true;
        expectingValue = false;
        return true;
    } else if (currentKey == "roles") {
        /* spec/66 §5.3 — an empty list is "authenticated, any role"; the ABSENCE
         * of the key is an error for a service with auth, so record that it was
         * written at all. */
        func->hasRoles = true;
        builder->pushState(std::make_unique<StringListBuilder>(builder, &func->roles));
        expectingKey = true;
        expectingValue = false;
        return true;
    } else if (currentKey == "cli") {
        builder->pushState(std::make_unique<StringListBuilder>(builder, &func->cli));
        expectingKey = true;
        expectingValue = false;
        return true;
    } else if (currentKey == "meta") {
        /* meta: [deprecated, ...] — free-form tags.  "deprecated" triggers a
         * compile-time warning at every call site. */
        builder->pushState(std::make_unique<StringListBuilder>(builder, &func->meta));
        expectingKey = true;
        expectingValue = false;
        return true;
    }
    return true;
}

bool FunctionBuilder::OnScalar(const std::string& value, const std::string& tag) {
    // spec/61 §3.2: inside object-form `return: { type: T, lifetime: producer }`.
    if (inReturnMap) {
        if (returnMapExpectKey) { returnMapKey = value; returnMapExpectKey = false; return true; }
        returnMapExpectKey = true;
        if (returnMapKey == "type") {
            func->returnType = value;
        } else if (returnMapKey == "lifetime") {
            // `producer` = the function produces a fresh owned value → the caller owns/drops it
            // (return-ownership). `scope` (default) = ordinary return (borrow/shared, not dropped).
            if (value == "producer") {
                func->ownsReturn = true;
            } else if (value == "scope") {
                func->ownsReturn = false;
            } else {
                LOG_ERROR("return lifetime must be scope|producer, got '" + value + "'");
            }
        } else if (returnMapKey == "borrows") {
            // spec/61 §8: names the ref<T> parameter the returned borrow is anchored to. The
            // modular borrow lifetime check is not enforced yet — accept the annotation so the
            // return type stays a plain borrow (behaves like a ptr-return today).
            func->returnBorrowsParam = value;
        } else {
            LOG_ERROR("unknown return modifier '" + returnMapKey + "' (expected type|lifetime|borrows)");
        }
        return true;
    }
    if (expectingKey) {
        currentKey = value;
        expectingKey = false;
        expectingValue = true;
        return true;
    }

    if (expectingValue) {
        if (currentKey == "name") func->name = value;
        else if (currentKey == "return") func->returnType = value;  // bare scalar form: `return: T`
        else if (currentKey == "version") {
            try { func->version = std::stoull(value); } catch (...) {}
        }
        else if (currentKey == "level") {   // spec/65 F65-19: pruefen, nicht durchsetzen
            int parsed = aiml::clearance::kAbsent;
            if (aiml::clearance::parseLevel(value, "def " + func->name, parsed))
                func->level = parsed;
        }
        else if (currentKey == "grants") {  // spec/65 F65-10
            int parsed = aiml::clearance::kAbsent;
            if (aiml::clearance::parseGrants(value, "def " + func->name, parsed))
                func->grants = parsed;
        }

        expectingKey = true;
        expectingValue = false;
        return true;
    }
    return false;
}

void FunctionBuilder::receiveChildNode(std::unique_ptr<Node> node) {
    if (currentKey == "body") {
        if (auto b = dynamic_cast<BlockStmt*>(node.release())) {
            func->body = std::unique_ptr<BlockStmt>(b);
        }
        expectingKey = true;
    }
}

std::unique_ptr<Node> FunctionBuilder::finish() {
    func->sourceLine = capturedLine;
    // Sanity-Check: ein `- def:` ohne `name:`-Feld erzeugt sonst eine
    // namenlose FunctionDecl, die als leerer Slot in __aiml_fptable landet
    // und beim Linken zu cross-module `__unnamed_<N>`-Bugs fuehrt
    // (siehe analysis Geschichte 2026-05-06). Lieber laut scheitern als
    // still kaputt linken.
    if (func->name.empty()) {
        LOG_ERROR("FunctionDecl ohne 'name'-Feld at line " +
                  std::to_string(capturedLine) +
                  ": ein '- def:'-Mapping muss mindestens 'name: <funktionsname>' enthalten. "
                  "Vermutlich ist hier ein verwaistes '- def:' uebrig (z.B. nach Refactor).");
    }
    // spec/65 F65-10: ein Gateway darf Privilegien nur fallen lassen.
    aiml::clearance::checkGrants(func->level, func->grants, "def " + func->name);
    return std::move(func);
}

} // namespace AST
} // namespace AIML
