#include "../clearance_exports.h"
#include "../../../logger.h"
#include "../ast_builder.h"
#include "extern_builder.h"

// Parses the continuation of a top-level `- extern: "C"` map item.
// ExternBuilder is pushed AFTER the ABI value ("C") has been consumed by
// RootState, so it never sees the outer map's OnMapStart.
//
// The remaining YAML events it receives look like:
//
//   OnScalar("defs")
//   OnSequenceStart
//     OnMapStart                     ← outer map for a single def item
//       OnScalar("def")              ← key
//       OnMapStart                   ← def body {name:…, args:…, return:…}
//         OnScalar("name"), OnScalar("SDL_Init")
//         OnScalar("args")
//         OnSequenceStart            ← args list [[flags, i32], …]
//           OnSequenceStart          ← [flags, i32]
//             OnScalar("flags"), OnScalar("i32")
//           OnSequenceEnd
//         OnSequenceEnd
//         OnScalar("return"), OnScalar("i32")
//       OnMapEnd                     ← end def body
//     OnMapEnd                       ← end def item outer map
//   OnSequenceEnd                    ← end defs list
//   OnMapEnd                         ← end outer map (the `- extern: …` item)

namespace AIML {
namespace AST {

// ── helpers ──────────────────────────────────────────────────────────────────

static void commitFunc(ExternBlock* block, ExternFuncDecl& f) {
    // Freie Funktion, kein Builder: der Ort kommt aus dem Parse-Merker
    // (spec/49 F49-100).  Ohne Datei bleibt das Praefix leer -- eine zugegebene
    // Luecke statt einer erfundenen Zeile.
    const std::string whereFile = AIML::AST::currentParseFile().empty()
        ? std::string() : (AIML::AST::currentParseFile() + ": ");
    // spec/65 F65-13: `extern` umgeht JEDE Symboltabelle des Uebersetzers von
    // Bauart — das Skript nennt die Signatur selbst, und der JIT loest den Namen
    // gegen den Prozess auf (LoadLibraryPermanently(nullptr)).  Eine
    // eingeschraenkte Sitzung darf darum nur Namen deklarieren, die der Wirt
    // ausdruecklich angeboten hat — die Export-Tabelle (spec/65 §5a).  Ohne Tabelle
    // ist die zulaessige Menge leer (fail closed), mit Tabelle entscheidet das Level.
    if (!f.name.empty() && aiml::clearance::restricted() &&
        !aiml::clearance::exportReachable(f.name)) {
        const int lvl = aiml::clearance::exportedLevel(f.name);
        if (lvl == aiml::clearance::kAbsent) {
            // Der Wirt bietet den Namen nicht an: das ist die `extern`-Zulassung (F65-13).
            LOG_ERROR(whereFile + "clearance-extern: `extern` auf '" + f.name + "' ist nicht zugelassen — "
                      "der Wirt bietet diesen Namen nicht an.  Eine eingeschraenkte Sitzung "
                      "darf nur Namen aus der Export-Tabelle deklarieren (spec/65 F65-13); "
                      "`extern` umgeht sonst jede Symboltabelle des Uebersetzers von Bauart.");
        } else {
            // Angeboten, aber ueber der Freigabe: das ist die ZUGRIFFSpruefung, und F65-12
            // verlangt, dass sie als VERWEIGERUNG erscheint — nicht unter einer zweiten
            // Kennung, nur weil der Weg hierher eine Deklaration war.
            LOG_ERROR(whereFile + "clearance-denied: `" + f.name + "` liegt auf Level " +
                      std::to_string(lvl) + ", diese Sitzung hat Freigabe " +
                      std::to_string(aiml::clearance::sessionClearance()) +
                      " (spec/65 F65-4: erreichbar ist nur, was `Freigabe <= Level` erfuellt).");
        }
    }
    if (!f.name.empty())
        block->funcs.push_back(f);
    f = ExternFuncDecl{};
}

// ── map events ────────────────────────────────────────────────────────────────

bool ExternBuilder::OnMapStart() {
    mapDepth++;

    if (state == State::SAW_DEF_KEY) {
        // Start of the def body {name: …, args: …, return: …}
        state = State::IN_DEF_BODY;
        defExpectKey = true;
        return true;
    }
    if (state == State::IN_DEFS) {
        // Start of a sequence item map — e.g. { def: { … } }
        state = State::IN_DEF_OUTER_MAP;
        defOuterExpectKey = true;
        return true;
    }
    // Other map starts (e.g. inline def: { name: … })
    if (state == State::IN_DEF_BODY) {
        // Nested map inside def body — skip
        skipDepth++;
    }
    return true;
}

bool ExternBuilder::OnMapEnd() {
    mapDepth--;

    if (skipDepth > 0) { skipDepth--; return true; }

    if (state == State::IN_DEF_BODY) {
        // Finished a def body → commit, back to defs-sequence level
        commitFunc(block.get(), currentFunc);
        state = State::IN_DEF_OUTER_MAP;
        return true;
    }
    if (state == State::IN_DEF_OUTER_MAP) {
        // Finished the outer {def: {…}} wrapper → back to defs list
        state = State::IN_DEFS;
        return true;
    }
    // Outermost map end — we're done
    builder->popState();
    return true;
}

// ── sequence events ───────────────────────────────────────────────────────────

bool ExternBuilder::OnSequenceStart() {
    if (state == State::IN_DEF_BODY && defCurrentKey == "args") {
        state = State::IN_ARGS;
        argPairDepth = 0;
        currentArgName = "";
        return true;
    }
    if (state == State::IN_ARGS) {
        argPairDepth++;   // entering a [name, type] inner pair
        currentArgName = "";
        return true;
    }
    if (state == State::OUTER && currentOuterKey == "defs") {
        state = State::IN_DEFS;
        return true;
    }
    return true;
}

bool ExternBuilder::OnSequenceEnd() {
    if (state == State::IN_ARGS) {
        if (argPairDepth > 0) {
            argPairDepth--;   // closed inner pair
            currentArgName = "";
        } else {
            // closed the outer args list → back to def body
            state = State::IN_DEF_BODY;
            defCurrentKey = "";
            defExpectKey  = true;
        }
        return true;
    }
    if (state == State::IN_DEFS) {
        // defs list closed → back to outer
        state = State::OUTER;
        return true;
    }
    return true;
}

// ── scalar events ─────────────────────────────────────────────────────────────

bool ExternBuilder::OnScalar(const std::string& value, const std::string& tag) {
    // ── inside an [argname, type] pair ──────────────────────────────────────
    if (state == State::IN_ARGS) {
        if (argPairDepth > 0) {
            // inside a [name, type] inner sequence
            if (currentArgName.empty()) {
                currentArgName = value;
            } else {
                currentFunc.args.push_back({currentArgName, value});
                currentArgName = "";
            }
        }
        // scalars at argPairDepth==0 would be oddly formatted; ignore
        return true;
    }

    // ── inside a def body {name:…, args:…, return:…} ─────────────────────
    if (state == State::IN_DEF_BODY) {
        if (defExpectKey) {
            defCurrentKey = value;
            defExpectKey  = false;
        } else {
            if (defCurrentKey == "name")        currentFunc.name       = value;
            else if (defCurrentKey == "return") currentFunc.returnType = value;
            defCurrentKey = "";
            defExpectKey  = true;
        }
        return true;
    }

    // ── inside the outer def-item map {def: …} ─────────────────────────────
    if (state == State::IN_DEF_OUTER_MAP) {
        if (defOuterExpectKey) {
            // Should be "def"
            defOuterExpectKey = false;
            if (value == "def") state = State::SAW_DEF_KEY;
        }
        return true;
    }

    // ── outer map (defs key, or unknown keys) ─────────────────────────────
    if (state == State::OUTER) {
        if (outerExpectKey) {
            currentOuterKey = value;
            outerExpectKey  = false;
        } else {
            // scalar value for currentOuterKey (unlikely for "defs:" which has seq value)
            outerExpectKey = true;
        }
        return true;
    }

    return true;
}

} // namespace AST
} // namespace AIML
