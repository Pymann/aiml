#include <map>
#include "../../../logger.h"
#include "../ast_builder.h"
#include "root_state.h"
#include "statement_builder.h"
#include "control_flow_builder.h"
#include "try_builder.h"
#include "class_builder.h"
#include "model_builder.h"
#include "interface_builder.h"
#include "enum_builder.h"
#include "cascade_builder.h"
#include "cascade_class_builder.h"
#include "classgroup_builder.h"
#include "groupinterface_builder.h"
#include "service_builder.h"
#include "groupmethods_builder.h"
#include "alias_builder.h"
#include "namespace_builder.h"
#include "import_builder.h"
#include "config_builder.h"
#include "function_builder.h"
#include "extern_builder.h"
#include "text_builder.h"
#include "prefab_builder.h"
#include "material_builder.h"

namespace AIML {
namespace AST {

bool RootState::OnMapStart() {
    expectingKey = true;
    return true;
}

bool RootState::OnMapEnd() {
    return true; 
}

class SkipBuilder : public StateHandler {
    int depth = 0;
public:
    SkipBuilder(Builder* b) : StateHandler(b) {}
    bool OnMapStart() override { depth++; return true; }
    bool OnSequenceStart() override { depth++; return true; }
    bool OnMapEnd() override { 
        if (depth == 0) builder->popState(); 
        else depth--; 
        return true; 
    }
    bool OnSequenceEnd() override { 
        if (depth == 0) builder->popState(); 
        else depth--; 
        return true; 
    }
    bool OnScalar(const std::string&, const std::string&) override { return true; }
};

bool RootState::OnScalar(const std::string& value, const std::string& tag) {
    if (expectingKey) {
        currentKey = value;
        expectingKey = false;
        
        // Push a new handler based on the top-level key
        if (currentKey == "def") {
             builder->pushState(std::make_unique<FunctionBuilder>(builder));
             return true;
        }
        // `free` fehlte hier bis 17.10.2026 und fiel damit in den SkipBuilder: ein
        // `- free: [p]` auf oberster Ebene wurde WORTLOS verworfen und nie ausgefuehrt
        // (gemessen in 8 Korpus-Tests, u.a. tests/test_oo.aiml:55).  Bei einer Sprache,
        // deren Speichermodell auf explizitem `new`/`free` steht, ist das kein Randfall.
        else if (currentKey == "decl" || currentKey == "set" || currentKey == "call" || currentKey == "print" || currentKey == "idx" || currentKey == "store" || currentKey == "free") {
             builder->pushState(std::make_unique<StatementBuilder>(builder, currentKey));
             return true;
        }
        else if (currentKey == "if" || currentKey == "while" || currentKey == "ret") {
             builder->pushState(std::make_unique<ControlFlowBuilder>(builder, currentKey));
             return true;
        }
        else if (currentKey == "try") {
             builder->pushState(std::make_unique<TryBuilder>(builder));
             return true;
        }
        else if (currentKey == "class") {
             builder->pushState(std::make_unique<ClassBuilder>(builder));
             return true;
        }
        else if (currentKey == "model") {
             builder->pushState(std::make_unique<ModelBuilder>(builder));
             return true;
        }
        else if (currentKey == "interface") {
             builder->pushState(std::make_unique<InterfaceBuilder>(builder));
             return true;
        }
        else if (currentKey == "enum") {
             builder->pushState(std::make_unique<EnumBuilder>(builder));
             return true;
        }
        else if (currentKey == "cascade") {
             builder->pushState(std::make_unique<CascadeBuilder>(builder));
             return true;
        }
        else if (currentKey == "cascade_class") {
             builder->pushState(std::make_unique<CascadeClassBuilder>(builder));
             return true;
        }
        else if (currentKey == "groupinterface") {   // spec/58 §12
             builder->pushState(std::make_unique<GroupInterfaceBuilder>(builder));
             return true;
        }
        else if (currentKey == "classgroup") {
             builder->pushState(std::make_unique<ClassGroupBuilder>(builder));
             return true;
        }
        else if (currentKey == "service") {
             builder->pushState(std::make_unique<ServiceBuilder>(builder));
             return true;
        }
        else if (currentKey == "groupmethods") {
             builder->pushState(std::make_unique<GroupMethodsBuilder>(builder));
             return true;
        }
        else if (currentKey == "alias") {
             builder->pushState(std::make_unique<AliasBuilder>(builder));
             return true;
        }
        else if (currentKey == "prefab") {
             builder->pushState(std::make_unique<PrefabBuilder>(builder));
             return true;
        }
        else if (currentKey == "material") {
             builder->pushState(std::make_unique<MaterialBuilder>(builder));
             return true;
        }
        else if (currentKey == "namespace") {
             builder->pushState(std::make_unique<NamespaceBuilder>(builder));
             return true;
        }
        else if (currentKey == "import") {
             builder->pushState(std::make_unique<ImportBuilder>(builder));
             return true;
        }
        else if (currentKey == "config") {
             builder->pushState(std::make_unique<ConfigBuilder>(builder));
             return true;
        }
        else if (currentKey == "text") {
             builder->pushState(std::make_unique<TextBuilder>(builder));
             return true;
        }
        else if (currentKey == "extern") {
            // ABI value ("C") comes as the scalar on the same line.
            // Do NOT push yet — wait for the value in the branch below.
            return true;
        }

        // spec/49 §2a, sinngemaess fuer die OBERSTE Ebene: ein unbekannter Schluessel
        // ist ein FEHLER, kein stilles Ueberspringen.  Bis 17.10.2026 landete hier
        // alles Unbekannte im SkipBuilder und verschwand wortlos — ein `- mdoel:`
        // liess das ganze Modell verschwinden, dieselbe Fehlerklasse wie das
        // verschluckte `exit:` (spec/23).  Weiterparsen bleibt, damit ein Lauf mehr
        // als einen Fehler meldet.
        {
            static const std::map<std::string, std::string> kTopHints = {
                {"clearance", "clearance-self-certify: die Freigabe setzt der WIRT, "
                              "nicht die Quelle (spec/65 F65-6)"},
                {"function",  "benutze `def:` (spec/07)"},
                {"struct",    "benutze `class:` oder `model:` (spec/02)"},
                {"type",      "benutze `alias:` (spec/02)"},
                {"include",   "benutze `import:` (spec/21)"},
                {"use",       "benutze `import:` (spec/21)"},
                {"main",      "`main` ist ein `def`: `- def: { name: main, ... }` (spec/08)"},
                {"return",    "`return:` gehoert in einen `def`-Rumpf (spec/07)"},
                {"throw",     "`throw:` gehoert in einen Rumpf, meist mit `try:` (spec/19)"},
                {"level",     "`level:` ist ein Modifier an einer Deklaration, kein "
                              "Top-Level-Knoten (spec/65 §8)"},
            };
            std::string msg = builder->fmtLoc() + "unknown top-level key '" +
                              currentKey + "' — not an aiml declaration.";
            auto h = kTopHints.find(currentKey);
            if (h != kTopHints.end()) msg += "  " + h->second;
            msg += "  (spec/49 §2a.1)";
            LOG_ERROR(msg);
        }

        // Unhandled top-level
        // Unhandled top-level
        builder->pushState(std::make_unique<SkipBuilder>(builder));
        return true;
    }

    // Value side (expectingKey == false)
    if (currentKey == "extern") {
        // value = ABI string, e.g. "C"
        builder->pushState(std::make_unique<ExternBuilder>(builder, value));
        currentKey   = "";
        expectingKey = false;  // ExternBuilder consumes the rest of this map item
        return true;
    }

    return false;
}

void RootState::receiveChildNode(std::unique_ptr<Node> node) {
    if (node) {
        LOG_DEBUG("RootState received child node.");
        builder->addDeclaration(std::move(node));
    }
}

} // namespace AST
} // namespace AIML
