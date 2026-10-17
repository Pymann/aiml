// spec/62 §13.5f — deklarierte Pipelines in die zusammengesetzte Form.
//
// Erzeugt werden je Schritt eine Rumpf-Funktion, optional eine Sonden-
// Funktion, und je Pipeline eine Funktion <name>() mit Rueckgabetyp
// `pipeline`, die alles zusammensetzt (F62-108).
//
// Die deklarierten Achsen erscheinen im Rumpf als lokale Variablen gleichen
// Namens (F62-109), gelesen mit step.arg — darum schreibt man `${arch}` und
// meint die Achse.  Ein `${NAME}`, das keine Achse und kein host-Name ist,
// wird dadurch zu einer undefinierten Variablen, und genau das ist die
// Pruefung, die F62-7 verlangt (F62-110) — ohne zweiten Pruefdurchlauf, der
// mit dem ersten uneins werden koennte.
#include "pipeline_expand.h"
#include "yaml_dump.h"
#include "../../logger.h"

#include <algorithm>
#include <set>
#include <sstream>
#include <vector>
#include <yaml-cpp/yaml.h>

namespace aiml {

namespace {

// Felder, die die Umschreibung NOCH NICHT abdeckt.  Sie werden abgelehnt und
// nicht ignoriert (F62-111): ein stillschweigend verworfenes `needs:` waere
// eine Pipeline, die in falscher Reihenfolge laeuft und dabei gruen meldet.
const char* kUnsupportedStepFields[] = { "outputs" };
const char* kUnsupportedStageFields[] = { "needs", "per", "runs_on", "outputs" };

std::string indentBlock(const std::string& text, const std::string& pad) {
    std::string out = pad;
    for (char ch : text) {
        out += ch;
        if (ch == '\n') out += pad;
    }
    return out;
}

// Ein YAML-Teilbaum als Fluss-Text, damit er in eine erzeugte Zeile passt.
std::string flowOf(const YAML::Node& node) {
    YAML::Emitter em;
    em << YAML::Flow;
    emitPreservingQuotes(em, node);
    const char* s = em.c_str();
    return s ? std::string(s) : std::string();
}

// Die Statements eines Rumpfs als Block-Sequenz, eingerueckt fuer `body:`.
std::string bodyBlock(const YAML::Node& seq, const std::string& pad) {
    std::string out;
    for (const auto& item : seq) {
        out += pad + "- " + flowOf(item) + "\n";
    }
    return out;
}

std::string scalarOr(const YAML::Node& node, const std::string& fallback) {
    return (node && node.IsScalar()) ? node.Scalar() : fallback;
}

bool isIdent(const std::string& name) {
    if (name.empty()) return false;
    if (!(isalpha((unsigned char)name[0]) || name[0] == '_')) return false;
    for (char ch : name)
        if (!(isalnum((unsigned char)ch) || ch == '_')) return false;
    return true;
}

} // namespace

bool sourceHasPipelineDecl(const std::string& text) {
    // Billig und vor dem Parsen: nur eine Deklaration auf oberster Ebene
    // beginnt eine Zeile mit "- pipeline:".
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        size_t pos = line.find_first_not_of(" \t");
        if (pos == std::string::npos) continue;
        if (line.compare(pos, 11, "- pipeline:") == 0) return true;
    }
    return false;
}

bool expandPipelineDeclarations(const std::string& text, const std::string& where,
                                std::string& out) {
    YAML::Node root;
    try {
        root = YAML::Load(text);
    } catch (const std::exception& e) {
        LOG_ERROR(where + ": pipeline-Umschreibung: " + e.what());
        return false;
    }
    if (!root.IsSequence()) return false;

    bool ok = true;
    std::string result;

    for (const auto& decl : root) {
        if (!decl.IsMap() || !decl["pipeline"]) {
            result += "- " + indentBlock(flowOf(decl), "  ").substr(2) + "\n";
            continue;
        }
        const YAML::Node pl = decl["pipeline"];
        const std::string pname = scalarOr(pl["name"], "");
        if (!isIdent(pname)) {
            LOG_ERROR(where + ": pipeline ohne gueltigen Namen — der Name wird eine "
                      "Funktion (spec/62 F62-108)");
            ok = false;
            continue;
        }

        // ── Achsen ──────────────────────────────────────────────────────────
        std::vector<std::pair<std::string, std::vector<std::string>>> axes;
        if (pl["params"] && pl["params"].IsMap()) {
            for (auto it = pl["params"].begin(); it != pl["params"].end(); ++it) {
                std::string axis = it->first.Scalar();
                std::vector<std::string> vals;
                if (it->second.IsSequence())
                    for (const auto& v : it->second) vals.push_back(v.Scalar());
                if (vals.empty()) {
                    LOG_ERROR(where + ": pipeline '" + pname + "': Achse '" + axis +
                              "' hat keine Werte — eine leere Domaene ergibt keine Lane "
                              "(spec/62 F62-2)");
                    ok = false;
                }
                axes.push_back({axis, vals});
            }
        }
        std::set<std::string> axisNames;
        for (const auto& ax : axes) axisNames.insert(ax.first);

        std::string ctor;    // der Rumpf der erzeugten Funktion <pname>()
        std::string helpers; // die erzeugten Rumpf-/Sonden-Funktionen
        // F62-9: Schrittnamen sind eindeutig, und `needs` nennt nur FRUEHERE
        // Schritte.  Hier zur Uebersetzungszeit, damit der Autor es erfaehrt,
        // bevor ein Lauf es merkt (die Laufzeit prueft es ebenfalls).
        std::set<std::string> declaredSteps;

        ctor += "      - decl: [pipe_obj, pipeline, { call: [pipeline.new] }]\n";
        ctor += "      - call: [pipe_obj.set_name, \"" + pname + "\"]\n";
        for (const auto& ax : axes) {
            const std::string var = "axis_" + ax.first;
            ctor += "      - decl: [" + var + ", list<string>]\n";
            for (const auto& v : ax.second)
                ctor += "      - call: [" + var + ".push, \"" + v + "\"]\n";
            ctor += "      - call: [pipe_obj.add_param, \"" + ax.first + "\", " + var + "]\n";
        }
        if (pl["exclude"] && pl["exclude"].IsSequence()) {
            for (const auto& ex : pl["exclude"]) {
                if (!ex.IsMap()) continue;
                std::string partial;
                for (auto it = ex.begin(); it != ex.end(); ++it) {
                    const std::string axis = it->first.Scalar();
                    if (!axisNames.count(axis)) {
                        LOG_ERROR(where + ": pipeline '" + pname + "': exclude nennt die Achse '" +
                                  axis + "', die die Pipeline nicht hat (spec/62 F62-4)");
                        ok = false;
                    }
                    if (!partial.empty()) partial += ",";
                    partial += axis + "=" + it->second.Scalar();
                }
                ctor += "      - call: [pipe_obj.add_exclude, \"" + partial + "\"]\n";
            }
        }
        if (pl["parallel"])
            ctor += "      - call: [pipe_obj.set_parallel, " + pl["parallel"].Scalar() + "]\n";
        if (pl["workspace_root"])
            ctor += "      - call: [pipe_obj.set_workspace_root, \"" +
                    pl["workspace_root"].Scalar() + "\"]\n";

        // ── Stufen und Schritte ─────────────────────────────────────────────
        int stageNo = 0;
        if (pl["stages"] && pl["stages"].IsSequence()) {
            for (const auto& stWrap : pl["stages"]) {
                const YAML::Node stg = stWrap["stage"] ? stWrap["stage"] : stWrap;
                for (const char* bad : kUnsupportedStageFields) {
                    if (stg[bad]) {
                        LOG_ERROR(where + ": pipeline '" + pname + "': stage-Feld '" + bad +
                                  "' wird noch nicht abgedeckt und darum abgelehnt "
                                  "(spec/62 F62-111)");
                        ok = false;
                    }
                }
                const std::string sname = scalarOr(stg["name"], "stage" + std::to_string(stageNo));
                const std::string svar  = "stage_" + std::to_string(stageNo);
                ctor += "      - decl: [" + svar + ", stage, { call: [stage.new] }]\n";
                ctor += "      - call: [" + svar + ".set_name, \"" + sname + "\"]\n";

                int stepNo = 0;
                if (stg["steps"] && stg["steps"].IsSequence()) {
                    for (const auto& spWrap : stg["steps"]) {
                        const YAML::Node sp = spWrap["step"] ? spWrap["step"] : spWrap;
                        for (const char* bad : kUnsupportedStepFields) {
                            if (sp[bad]) {
                                LOG_ERROR(where + ": pipeline '" + pname + "': step-Feld '" + bad +
                                          "' wird noch nicht abgedeckt und darum abgelehnt "
                                          "(spec/62 F62-111)");
                                ok = false;
                            }
                        }
                        const std::string stepName =
                            scalarOr(sp["name"], "step" + std::to_string(stepNo));
                        const std::string cache = scalarOr(sp["cache"], "opaque");
                        const std::string perMode = scalarOr(sp["per"], "lane");
                        if (perMode != "lane" && perMode != "once") {
                            LOG_ERROR(where + ": pipeline '" + pname + "': Schritt '" + stepName +
                                      "': per ist 'lane' oder 'once', nicht '" + perMode +
                                      "' (spec/62 §5.5)");
                            ok = false;
                        }
                        // F62-115: ein einmaliger Schritt kann nicht von einer
                        // Achse haengen — es gibt EIN Ergebnis fuer die ganze
                        // Matrix.  Die Alternative ("nimm die erste Lane") waere
                        // ein Ergebnis, das von der Lane-Reihenfolge abhaengt.
                        if (perMode == "once" && sp["args"] && sp["args"].IsSequence() &&
                            sp["args"].size() > 0) {
                            LOG_ERROR(where + ": pipeline '" + pname + "': Schritt '" + stepName +
                                      "' ist 'per: once' und deklariert trotzdem Achsen — ein "
                                      "einmaliger Schritt kann nicht von einer Achse haengen "
                                      "(spec/62 F62-115)");
                            ok = false;
                        }
                        if (!declaredSteps.insert(stepName).second) {
                            LOG_ERROR(where + ": pipeline '" + pname + "': der Schrittname '" +
                                      stepName + "' kommt zweimal vor — dann ist nicht "
                                      "entscheidbar, welchen Schritt ein 'needs' meint "
                                      "(spec/62 F62-9)");
                            ok = false;
                        }
                        std::vector<std::string> needNames;
                        if (sp["needs"] && sp["needs"].IsSequence()) {
                            for (const auto& nd : sp["needs"]) {
                                const std::string need = nd.Scalar();
                                if (!declaredSteps.count(need)) {
                                    LOG_ERROR(where + ": pipeline '" + pname + "': Schritt '" +
                                              stepName + "' braucht '" + need + "' — das ist kein "
                                              "frueher deklarierter Schritt (spec/62 F62-9)");
                                    ok = false;
                                    continue;
                                }
                                needNames.push_back(need);
                            }
                        }
                        const std::string fnBase = "__pl_" + pname + "_" +
                                                   std::to_string(stageNo) + "_" +
                                                   std::to_string(stepNo);

                        // Deklarierte Achsen + host-Namen werden zu lokalen
                        // Variablen im Rumpf (F62-109).
                        std::string prelude;
                        std::vector<std::string> declaredArgs;
                        if (sp["args"] && sp["args"].IsSequence()) {
                            for (const auto& a : sp["args"]) {
                                const std::string axis = a.Scalar();
                                if (!axisNames.count(axis)) {
                                    LOG_ERROR(where + ": pipeline '" + pname + "': Schritt '" +
                                              stepName + "' benutzt die Achse '" + axis +
                                              "', die die Pipeline nicht hat (spec/62 F62-1)");
                                    ok = false;
                                }
                                declaredArgs.push_back(axis);
                                prelude += "      - decl: [" + axis +
                                           ", string, { call: [step.arg, \"" + axis + "\"] }]\n";
                            }
                        }
                        std::vector<std::string> hostNames;
                        if (sp["host"] && sp["host"].IsSequence()) {
                            for (const auto& h : sp["host"]) {
                                const std::string hname = h.Scalar();
                                // F62-17: host traegt NAMEN, nie name=value — ein
                                // Wert im Quelltext waere ein Geheimnis im Quelltext.
                                if (hname.find('=') != std::string::npos) {
                                    LOG_ERROR(where + ": pipeline '" + pname + "': Schritt '" +
                                              stepName + "': host nennt '" + hname +
                                              "' — host traegt Namen, nie name=value "
                                              "(spec/62 F62-17)");
                                    ok = false;
                                    continue;
                                }
                                hostNames.push_back(hname);
                                prelude += "      - decl: [" + hname +
                                           ", string, { call: [step.host, \"" + hname + "\"] }]\n";
                            }
                        }
                        // F62-33/F62-121: die Ergebnisse der `needs` erscheinen als
                        // lokale Namen gleich dem Schrittnamen — darum schreibt man
                        // `${clone}` und meint den Wert, den `clone` veroeffentlicht
                        // hat.  Ein Name, der schon eine Achse ist, waere zweimal
                        // deklariert; das ist ein Fehler und keine Vorrangregel.
                        for (const auto& need : needNames) {
                            if (std::find(declaredArgs.begin(), declaredArgs.end(), need) !=
                                declaredArgs.end()) {
                                LOG_ERROR(where + ": pipeline '" + pname + "': Schritt '" +
                                          stepName + "': '" + need + "' ist Achse UND needs — "
                                          "dann waere der Name im Rumpf zweimal belegt");
                                ok = false;
                                continue;
                            }
                            prelude += "      - decl: [" + need +
                                       ", string, { call: [step.need_result, \"" + need + "\"] }]\n";
                        }
                        prelude += "      - decl: [workspace, string, { call: [step.workspace] }]\n";

                        const std::string retType = scalarOr(sp["return"], "i32");
                        helpers += "- def:\n";
                        helpers += "    name: " + fnBase + "_body\n";
                        helpers += "    return: " + retType + "\n";
                        helpers += "    body:\n";
                        helpers += prelude;
                        if (sp["body"] && sp["body"].IsSequence())
                            helpers += bodyBlock(sp["body"], "      ");
                        else
                            helpers += "      - return: 0\n";
                        helpers += "\n";

                        // F62-121: aus `return: <typ>` wird eine Huelle, die den
                        // geschriebenen Rumpf ruft und sein Ergebnis in den Kanal
                        // legt (§13.5h).  Der Autor schreibt weiter `- return: rep`
                        // und muss von set_result nichts wissen.  Nur fuer Typen,
                        // die der Kanal traegt: Zeichenkette oder Zahl (F62-118).
                        const bool publishable =
                            (retType == "string" || retType == "i32" || retType == "i64" ||
                             retType == "u32" || retType == "u64");
                        std::string bundledFn = fnBase + "_body";
                        if (publishable) {
                            helpers += "- def:\n";
                            helpers += "    name: " + fnBase + "_pub\n";
                            helpers += "    return: i32\n";
                            helpers += "    body:\n";
                            helpers += "      - decl: [pub_val, " + retType + ", { call: [" +
                                       fnBase + "_body] }]\n";
                            helpers += "      - call: [step.set_result, pub_val]\n";
                            helpers += "      - return: 0\n";
                            helpers += "\n";
                            bundledFn = fnBase + "_pub";
                        }
                        bool wantPure   = (cache == "pure");
                        bool assertPure = sp["assert_pure"] && sp["assert_pure"].Scalar() == "true";
                        std::string bundleOpts = "{ context: true";
                        if (wantPure)   bundleOpts += ", pure: true";
                        if (assertPure) bundleOpts += ", assert_pure: true";
                        bundleOpts += " }";

                        if (sp["resolve"] && sp["resolve"].IsSequence()) {
                            helpers += "- def:\n";
                            helpers += "    name: " + fnBase + "_resolve\n";
                            helpers += "    return: { type: string, lifetime: producer }\n";
                            helpers += "    body:\n";
                            helpers += prelude;
                            helpers += bodyBlock(sp["resolve"], "      ");
                            helpers += "\n";
                        }

                        const std::string pvar = "step_" + std::to_string(stageNo) + "_" +
                                                 std::to_string(stepNo);
                        ctor += "      - decl: [" + pvar + ", step, { call: [step.new] }]\n";
                        ctor += "      - call: [" + pvar + ".set_name, \"" + stepName + "\"]\n";
                        ctor += "      - call: [" + pvar + ".set_body, { call: [sys.bundle, \"" +
                                bundledFn + "\", " + bundleOpts + "] }]\n";
                        ctor += "      - call: [" + pvar + ".set_cache, CacheClass." + cache + "]\n";
                        for (const auto& axis : declaredArgs)
                            ctor += "      - call: [" + pvar + ".use_arg, \"" + axis + "\"]\n";
                        if (sp["resolve"])
                            ctor += "      - call: [" + pvar + ".set_resolve, " + fnBase +
                                    "_resolve]\n";
                        if (sp["on_fail"]) {
                            std::string reach = sp["on_fail"].Scalar();
                            // F62-94: `continue` ist in aiml ein Schluesselwort, der
                            // Enum-Wert heisst `proceed`.  Derselbe Begriff, zwei
                            // Schreibweisen — hier ist die Umschreibstelle.
                            if (reach == "continue") reach = "proceed";
                            ctor += "      - call: [" + pvar + ".set_on_fail, OnFail." + reach + "]\n";
                        }
                        if (sp["timeout"])
                            ctor += "      - call: [" + pvar + ".set_timeout, " +
                                    sp["timeout"].Scalar() + "]\n";
                        if (sp["retry"])
                            ctor += "      - call: [" + pvar + ".set_retry, " +
                                    sp["retry"].Scalar() + "]\n";
                        if (sp["pause_before"] && sp["pause_before"].Scalar() == "true")
                            ctor += "      - call: [" + pvar + ".set_pause_before, 1]\n";
                        if (perMode == "once")
                            ctor += "      - call: [" + pvar + ".set_per, Per.once]\n";
                        for (const auto& need : needNames)
                            ctor += "      - call: [" + pvar + ".add_need, \"" + need + "\"]\n";
                        ctor += "      - call: [" + svar + ".add_step, " + pvar + "]\n";
                        ++stepNo;
                    }
                }
                ctor += "      - call: [pipe_obj.add_stage, " + svar + "]\n";
                ++stageNo;
            }
        }
        ctor += "      - return: pipe_obj\n";

        result += helpers;
        result += "- def:\n";
        result += "    name: " + pname + "\n";
        result += "    return: pipeline\n";
        result += "    body:\n";
        result += ctor;
        result += "\n";
    }

    if (!ok) return false;
    out = result;
    return true;
}

} // namespace aiml
