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
#include <array>
#include <map>
#include <set>
#include <cstdlib>
#include <sstream>
#include <vector>
#include <yaml-cpp/yaml.h>

namespace aiml {

namespace {

// Felder, die die Umschreibung NOCH NICHT abdeckt.  Sie werden abgelehnt und
// nicht ignoriert (F62-111): ein stillschweigend verworfenes `needs:` waere
// eine Pipeline, die in falscher Reihenfolge laeuft und dabei gruen meldet.
const char* kUnsupportedStepFields[] = { "outputs" };
const char* kUnsupportedStageFields[] = { "needs", "per", "outputs" };

// Felder, die eine LAUFZEIT verlangen (spec/62 §18.1 F62-124).  Sie werden
// nicht „noch nicht abgedeckt" genannt, denn das waere die falsche Auskunft:
// die Form ist festgelegt, nur ausfuehren kann sie dieser Lauf nicht.  Ein
// stiller Rueckfall auf das Dateisystem des Entwicklers waere schlimmer als
// ein Lauf, der gar nicht startet — er hiesse „gegen ubuntu:24.04 gebaut" und
// waere es nicht.
struct RuntimeField { const char* name; const char* what; };
const RuntimeField kRuntimeFields[] = {
    { "image", "ein Wurzel-Dateisystem" },
    { "push",  "einen Ort, an den ein Abbild geschrieben wird" },
};

// Wie ein Feld genannt wird, das eine Laufzeit braucht.
std::string runtimeFieldMessage(const std::string& where, const std::string& pname,
                                const char* field, const char* what, const char* level) {
    return where + ": pipeline '" + pname + "': " + level + "-Feld '" + field +
           "' verlangt " + what + " — dieser Lauf ist LOKAL und hat keine Laufzeit "
           "(spec/62 §18.1 F62-124).  Entweder das Feld weglassen (dann laeuft die "
           "Pipeline gegen diesen Rechner, F62-125), oder den Lauf ueber einen Wirt "
           "starten, der Container kann (cosmos doc/spec/110).";
}

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

// ── spec/62 §18 + §20: die verteilten Schluessel ────────────────────────────
//
// Sie werden UEBERSETZT und nicht abgelehnt (F62-124a): dieselbe Datei ist
// fuer einen Cluster gueltig, und ein Kontroller soll sie lesen statt selbst
// zu parsen (§20.1).  Abgelehnt wird erst der lokale LAUF
// (`aiml_pipeline_local_refusal`).
//
// Alte Namen werden beim Namen genannt (F110-127): zwei Schreibweisen fuer
// dieselbe Sache sind die Redundanz, die spaeter niemand mehr aufloest.
struct OldName { const char* old_key; const char* now; const char* why; };
const OldName kOldNames[] = {
    { "runs_on",   "agent",     "" },
    { "label",     "agent",     "" },
    { "runs_in",   "steplayer", "" },
    // Nicht nur ein anderer Name, auch eine andere EINHEIT: `timeout:` zaehlt
    // in MILLISEKUNDEN (F62-40).  Wer `timeout_s: 5` schreibt und `timeout: 5`
    // bekommt, wartet fuenf Millisekunden — die Meldung muss die Einheit
    // nennen, nicht nur den Namen.
    { "timeout_s", "timeout",   " — und zwar in MILLISEKUNDEN (spec/62 F62-40)" },
};

// Was auf PIPELINE-Ebene bzw. an einer STUFE stehen darf.  Eine Erlaubnisliste
// und keine Sperrliste: ein unbekannter Schluessel ist ein Fehler (§20.5), und
// genau das macht die Pruefung des Kontrollers entbehrlich.
const char* kPipelineKeys[] = {
    "name", "params", "exclude", "parallel", "workspace_root", "steps", "host",
    "agent", "steplayer", "image", "base", "requires", "shares", "resources",
    "timeout", "push", "runtime",
};
const char* kStageKeys[] = {
    "name", "steps", "run", "args", "host", "cache", "on_fail", "timeout",
    "retry", "exec_defaults", "agent", "steplayer", "image", "base",
    "requires", "shares", "resources",
};

bool knownKey(const std::string& key, const char* const* list, size_t n) {
    for (size_t i = 0; i < n; ++i) if (key == list[i]) return true;
    return false;
}

// Text ODER Liste → eine Zeile, Komma-getrennt (`requires:`, F62-126).
std::string csvOf(const YAML::Node& node) {
    if (!node) return std::string();
    if (node.IsSequence()) {
        std::string out;
        for (const auto& item : node) {
            if (!out.empty()) out += ",";
            out += item.Scalar();
        }
        return out;
    }
    return node.IsScalar() ? node.Scalar() : std::string();
}

// `shares:` → "name:ziel:modus,…" (F62-136).  Der NAME ist ein Name, nie ein
// Pfad: wo die Ablage liegt, weiss der Wirt — eine Pipeline, die einen Pfad
// nennt, holt sich jedes Verzeichnis des Wirts in ihren Container.
std::string sharesOf(const YAML::Node& node, const std::string& where,
                     const std::string& pname, const std::string& level, bool& ok) {
    if (!node) return std::string();
    if (!node.IsSequence()) {
        LOG_ERROR(where + ": pipeline '" + pname + "': " + level + "-Feld 'shares' ist eine "
                  "Liste von { name, to, mode } (spec/62 §18.1b)");
        ok = false;
        return std::string();
    }
    std::string out;
    for (const auto& row : node) {
        const std::string name = (row["name"] && row["name"].IsScalar()) ? row["name"].Scalar() : "";
        const std::string dest = (row["to"]   && row["to"].IsScalar())   ? row["to"].Scalar()   : "";
        std::string mode = (row["mode"] && row["mode"].IsScalar()) ? row["mode"].Scalar() : "ro";
        if (name.empty() || dest.empty()) {
            LOG_ERROR(where + ": pipeline '" + pname + "': " + level + "-Feld 'shares' braucht "
                      "je Eintrag 'name' und 'to' (spec/62 §18.1b F62-136)");
            ok = false;
            continue;
        }
        if (name.find('/') != std::string::npos) {
            LOG_ERROR(where + ": pipeline '" + pname + "': Freigabe 'name: " + name + "' ist ein "
                      "PFAD — eine Freigabe wird mit ihrem Namen genannt, den Ort kennt der Wirt "
                      "(spec/62 F62-136)");
            ok = false;
            continue;
        }
        if (mode != "ro" && mode != "rw") {
            LOG_ERROR(where + ": pipeline '" + pname + "': Freigabe '" + name + "' hat 'mode: " +
                      mode + "' — erlaubt sind 'ro' und 'rw' (spec/62 §18.1b)");
            ok = false;
            continue;
        }
        if (!out.empty()) out += ",";
        out += name + ":" + dest + ":" + mode;
    }
    return out;
}

// Das Ziel einer Freigabezeile ("name:ziel:modus" → "ziel").
std::string shareDest(const std::string& row) {
    size_t first = row.find(':');
    if (first == std::string::npos) return row;
    size_t second = row.find(':', first + 1);
    return row.substr(first + 1, second == std::string::npos ? std::string::npos
                                                             : second - first - 1);
}

// Freigaben addieren sich, bei gleichem ZIEL gewinnt die Stufe (F62-138).
std::string mergeShares(const std::string& at_pipe, const std::string& at_stage) {
    if (at_stage.empty()) return at_pipe;
    if (at_pipe.empty())  return at_stage;
    std::vector<std::string> mine;
    std::stringstream in(at_stage);
    std::string row;
    while (std::getline(in, row, ',')) if (!row.empty()) mine.push_back(row);
    std::string out;
    std::stringstream pin(at_pipe);
    while (std::getline(pin, row, ',')) {
        if (row.empty()) continue;
        bool taken = false;
        for (const auto& own : mine) if (shareDest(own) == shareDest(row)) taken = true;
        if (taken) continue;
        if (!out.empty()) out += ",";
        out += row;
    }
    for (const auto& own : mine) {
        if (!out.empty()) out += ",";
        out += own;
    }
    return out;
}

// `mem:` traegt eine EINHEIT ("512M", "8G") — ohne waere „8" zweideutig, und
// eine Zweideutigkeit in einer Grenze ist eine Falle (F62-133).
int32_t memMb(const std::string& text, const std::string& where,
              const std::string& pname, bool& ok) {
    if (text.empty()) return 0;
    char unit = text[text.size() - 1];
    int32_t num = atoi(text.c_str());
    if (unit == 'G' || unit == 'g') return num * 1024;
    if (unit == 'M' || unit == 'm') return num;
    LOG_ERROR(where + ": pipeline '" + pname + "': 'mem: " + text + "' ohne Einheit — "
              "'512M' oder '8G' (spec/62 F62-133)");
    ok = false;
    return num;
}

int32_t numAt(const YAML::Node& node, const char* key) {
    if (!node || !node[key] || !node[key].IsScalar()) return 0;
    return atoi(node[key].Scalar().c_str());
}

std::string textAt(const YAML::Node& node, const char* key) {
    if (!node || !node[key] || !node[key].IsScalar()) return std::string();
    return node[key].Scalar();
}

// Ein Wert, der als LITERAL in erzeugten Quelltext geht, wird MASKIERT.
// Ohne das endet das Literal mitten im Wert und der Rest ist aiml — bei einer
// EINGEREICHTEN Pipeline (cosmos doc/spec/96) waere das eingeschleuster Code.
// Maskiert und nicht abgewiesen, weil ein `run:` legitim Anfuehrungszeichen
// traegt: `--format="%h"` ist eine gewoehnliche Befehlszeile.
std::string yamlQuoted(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 2);
    for (char ch : value) {
        switch (ch) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': break;
            case '\t': out += "\\t";  break;
            default:   out += ch;
        }
    }
    return out;
}

// Eine Zeile `stage_N.set_attr "schluessel" "wert"`, nur wenn etwas da ist.
std::string attrLine(const std::string& var, const char* key, const std::string& value) {
    if (value.empty()) return std::string();
    return "      - call: [" + var + ".set_attr, \"" + std::string(key) + "\", \"" +
           yamlQuoted(value) + "\"]\n";
}

std::string limitLine(const std::string& var, const char* key, int32_t value) {
    if (value == 0) return std::string();
    return "      - call: [" + var + ".set_limit, \"" + std::string(key) + "\", " +
           std::to_string(value) + "]\n";
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
        if (pname.empty()) {
            LOG_ERROR(where + ": pipeline ohne Namen — der Name wird eine Funktion "
                      "(spec/62 F62-108)");
            ok = false;
            continue;
        }
        // F62-157: der NAME einer Pipeline ist Text, die FUNKTION braucht einen
        // Bezeichner.  Ist der Name schon einer, heisst die Funktion genau so
        // (F62-108, so ruft man sie lokal).  Sonst entsteht ein abgeleiteter
        // Funktionsname — `name: 9rooms_vom_stapel` faengt mit einer Ziffer an
        // und war damit bisher gar nicht uebersetzbar, obwohl der Name voellig
        // in Ordnung ist.  Der Wert traegt weiter den ECHTEN Namen (`pipe.name`),
        // und ein Wirt findet die Funktion ueber `cx.names` (spec/29_2 F29_2-…i).
        std::string fname = pname;
        if (!isIdent(fname)) {
            std::string safe;
            for (char ch : fname)
                safe += (isalnum((unsigned char)ch) || ch == '_') ? ch : '_';
            if (safe.empty() || !(isalpha((unsigned char)safe[0]) || safe[0] == '_'))
                safe = "pl_" + safe;
            fname = safe;
            LOG_INFO(where + ": pipeline '" + pname + "': der Name ist kein Bezeichner — die "
                     "erzeugte Funktion heisst '" + fname + "' (spec/62 F62-157); der Lauf "
                     "traegt weiter den Namen '" + pname + "'");
        }
        // Alte Namen beim Namen nennen (F110-127) — am Kopf wie an der Stufe.
        for (const OldName& on : kOldNames) {
            if (pl[on.old_key]) {
                LOG_ERROR(where + ": pipeline '" + pname + "': `" + on.old_key +
                          ":` heisst jetzt `" + on.now + ":`" + on.why + " — zwei "
                          "Schreibweisen fuer dieselbe Sache loest spaeter niemand mehr auf "
                          "(spec/62 §18.5)");
                ok = false;
            }
        }
        // F62-57: eine Pipeline hat `steps:` (darin Stufen), eine Stufe hat
        // `steps:` (darin Schritte).  `stages:` las die Umschreibung bis zum
        // 25.10.2026 — die Spec sagt `steps:`, also sagt es die Umschreibung.
        if (pl["stages"]) {
            LOG_ERROR(where + ": pipeline '" + pname + "': `stages:` heisst `steps:` — eine "
                      "Pipeline listet unter `steps:` ihre Stufen, eine Stufe darunter ihre "
                      "Schritte (spec/62 F62-57)");
            ok = false;
        }
        if (pl.IsMap()) {
            for (auto it = pl.begin(); it != pl.end(); ++it) {
                const std::string key = it->first.Scalar();
                if (knownKey(key, kPipelineKeys, sizeof(kPipelineKeys)/sizeof(kPipelineKeys[0])))
                    continue;
                if (key == "stages") continue;   // schon gemeldet
                bool is_old = false;
                for (const OldName& on : kOldNames) if (key == on.old_key) is_old = true;
                if (is_old) continue;            // schon gemeldet
                LOG_ERROR(where + ": pipeline '" + pname + "': unbekannter Schluessel `" + key +
                          ":` — was der Uebersetzer nicht kennt, ist ein Fehler und keine "
                          "stille Wirkungslosigkeit (spec/62 §20.5)");
                ok = false;
            }
        }

        // Die Kopfwerte: Vorgabe fuer jede Stufe (§20.3).  Aufgeloest wird
        // HIER, einmal — ein Leser, der nachrechnet, ist ein zweiter Parser
        // (F62-151).
        const std::string headAgent     = textAt(pl, "agent");
        const std::string headImage     = textAt(pl, "image");
        const std::string headBase      = textAt(pl, "base");
        const std::string headLayer     = textAt(pl, "steplayer");
        const std::string headRequires  = csvOf(pl["requires"]);
        const std::string headShares    = sharesOf(pl["shares"], where, pname, "pipeline", ok);
        const int32_t     headTimeout   = numAt(pl, "timeout");
        const YAML::Node  headRes       = pl["resources"];
        const int32_t     headCpu       = numAt(headRes, "cpu");
        const int32_t     headPids      = numAt(headRes, "pids");
        const int32_t     headMem       = memMb(textAt(headRes, "mem"), where, pname, ok);

        // ── Achsen ──────────────────────────────────────────────────────────
        //
        // spec/62 §4.1: ein Eintrag ist `[name, typ, [werte]]`.  Der TYP
        // zaehlt (F62-1): im Rumpf ist `jobs` dann ein i32, mit dem man
        // rechnen kann, und keine Umgebungs-Zeichenkette.  Die alte Abbildung
        // `arch: [x86_64, arm64]` wird abgewiesen und nennt die Liste — zwei
        // Schreibweisen fuer dieselbe Sache loest spaeter niemand mehr auf
        // (§20.5).
        struct Axis { std::string name; std::string type; std::vector<std::string> values; };
        std::vector<Axis> axes;
        if (pl["params"] && pl["params"].IsMap()) {
            LOG_ERROR(where + ": pipeline '" + pname + "': `params:` ist eine LISTE von "
                      "`[name, typ, [werte]]` (spec/62 §4.1), keine Abbildung "
                      "`arch: [...]` — der Typ einer Achse zaehlt (F62-1)");
            ok = false;
        } else if (pl["params"] && pl["params"].IsSequence()) {
            for (const auto& row : pl["params"]) {
                if (!row.IsSequence() || row.size() < 3) {
                    LOG_ERROR(where + ": pipeline '" + pname + "': ein `params:`-Eintrag ist "
                              "`[name, typ, [werte]]` (spec/62 §4.1)");
                    ok = false;
                    continue;
                }
                Axis ax;
                ax.name = row[0].Scalar();
                ax.type = row[1].Scalar();
                if (row[2].IsSequence())
                    for (const auto& v : row[2]) ax.values.push_back(v.Scalar());
                if (ax.name.empty() || !isIdent(ax.name)) {
                    LOG_ERROR(where + ": pipeline '" + pname + "': Achsenname '" + ax.name +
                              "' ist kein Bezeichner — er wird im Rumpf eine lokale Variable "
                              "(spec/62 F62-109)");
                    ok = false;
                    continue;
                }
                if (ax.type != "string" && ax.type != "i32" && ax.type != "i64" &&
                    ax.type != "f32" && ax.type != "bool") {
                    LOG_ERROR(where + ": pipeline '" + pname + "': Achse '" + ax.name +
                              "': Typ '" + ax.type + "' kann keine Domaene aus Literalen "
                              "tragen — string, i32, i64, f32 oder bool (spec/62 F62-1)");
                    ok = false;
                    continue;
                }
                if (ax.values.empty()) {
                    LOG_ERROR(where + ": pipeline '" + pname + "': Achse '" + ax.name +
                              "' hat keine Werte — eine leere Domaene ergibt keine Lane "
                              "(spec/62 F62-2)");
                    ok = false;
                }
                axes.push_back(ax);
            }
        }
        std::set<std::string> axisNames;
        std::map<std::string, std::string> axisTypes;
        for (const auto& ax : axes) { axisNames.insert(ax.name); axisTypes[ax.name] = ax.type; }

        std::string ctor;    // der Rumpf der erzeugten Funktion <pname>()
        std::string helpers; // die erzeugten Rumpf-/Sonden-Funktionen
        // F62-9: Schrittnamen sind eindeutig, und `needs` nennt nur FRUEHERE
        // Schritte.  Hier zur Uebersetzungszeit, damit der Autor es erfaehrt,
        // bevor ein Lauf es merkt (die Laufzeit prueft es ebenfalls).
        std::set<std::string> declaredSteps;

        ctor += "      - decl: [pipe_obj, pipeline, { call: [pipeline.new] }]\n";
        ctor += "      - call: [pipe_obj.set_name, \"" + pname + "\"]\n";
        for (const auto& ax : axes) {
            const std::string var = "axis_" + ax.name;
            ctor += "      - decl: [" + var + ", list<string>]\n";
            for (const auto& v : ax.values)
                ctor += "      - call: [" + var + ".push, \"" + yamlQuoted(v) + "\"]\n";
            ctor += "      - call: [pipe_obj.add_param, \"" + ax.name + "\", \"" + ax.type +
                    "\", " + var + "]\n";
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
                ctor += "      - call: [pipe_obj.add_exclude, \"" + yamlQuoted(partial) + "\"]\n";
            }
        }
        // `push:` — wohin ein Abbild geht.  `from` nennt eine STUFE; geprueft
        // wird nach den Stufen (dort ist bekannt, welche es gibt).
        std::vector<std::array<std::string,3>> pushes;
        if (pl["push"] && pl["push"].IsSequence()) {
            for (const auto& row : pl["push"]) {
                std::array<std::string,3> one = { textAt(row, "from"), textAt(row, "to"),
                                                  textAt(row, "tag") };
                if (one[0].empty() || one[1].empty()) {
                    LOG_ERROR(where + ": pipeline '" + pname + "': `push:` braucht je Eintrag "
                              "'from' (eine Stufe) und 'to' (spec/62 §18, F62-71)");
                    ok = false;
                    continue;
                }
                pushes.push_back(one);
            }
        }
        if (pl["parallel"])
            ctor += "      - call: [pipe_obj.set_parallel, " + pl["parallel"].Scalar() + "]\n";
        if (pl["workspace_root"])
            ctor += "      - call: [pipe_obj.set_workspace_root, \"" +
                    pl["workspace_root"].Scalar() + "\"]\n";

        // ── Stufen und Schritte ─────────────────────────────────────────────
        int stageNo = 0;
        std::set<std::string> declaredStages;
        if (pl["steps"] && pl["steps"].IsSequence()) {
            for (const auto& stWrap : pl["steps"]) {
                const YAML::Node stg = stWrap["stage"] ? stWrap["stage"] : stWrap;
                for (const char* bad : kUnsupportedStageFields) {
                    if (stg[bad]) {
                        LOG_ERROR(where + ": pipeline '" + pname + "': stage-Feld '" + bad +
                                  "' wird noch nicht abgedeckt und darum abgelehnt "
                                  "(spec/62 F62-111)");
                        ok = false;
                    }
                }
                for (const OldName& on : kOldNames) {
                    if (stg[on.old_key]) {
                        LOG_ERROR(where + ": pipeline '" + pname + "': stage-Feld `" +
                                  on.old_key + ":` heisst jetzt `" + on.now + ":" + on.why +
                                  "` (spec/62 §18.5)");
                        ok = false;
                    }
                }
                if (stg.IsMap()) {
                    for (auto it = stg.begin(); it != stg.end(); ++it) {
                        const std::string key = it->first.Scalar();
                        if (knownKey(key, kStageKeys, sizeof(kStageKeys)/sizeof(kStageKeys[0])))
                            continue;
                        bool known_bad = false;
                        for (const char* bad : kUnsupportedStageFields) if (key == bad) known_bad = true;
                        for (const OldName& on : kOldNames) if (key == on.old_key) known_bad = true;
                        if (known_bad) continue;
                        LOG_ERROR(where + ": pipeline '" + pname + "': unbekannter Schluessel `" +
                                  key + ":` an einer Stufe (spec/62 §20.5)");
                        ok = false;
                    }
                }
                // spec/62 §18.2 (F62-126): was die Umgebung mitbringen muss.
                // Lokal ist das die beste Stelle fuer eine Pruefung — der Lauf
                // laeuft ja GEGEN diesen Rechner (F62-125).  Geprueft wird vor
                // dem ersten Schritt, mit `sys.require_tools` (spec/23 §5.6),
                // damit es EINE Pruefung gibt und nicht zwei (F23-43).
                std::string stageRequires;
                if (stg["requires"]) {
                    if (stg["requires"].IsSequence()) {
                        for (const auto& r : stg["requires"]) {
                            if (!stageRequires.empty()) stageRequires += ",";
                            stageRequires += r.Scalar();
                        }
                    } else {
                        stageRequires = stg["requires"].Scalar();
                    }
                }
                const std::string sname = scalarOr(stg["name"], "stage" + std::to_string(stageNo));
                const std::string svar  = "stage_" + std::to_string(stageNo);
                declaredStages.insert(sname);
                // Eine Stufe traegt EINEN Befehl oder SCHRITTE, nie beides
                // (cosmos F106-11): sonst ist nicht entscheidbar, was zuerst
                // laeuft und was den Ausgang bestimmt.
                if (stg["run"] && stg["steps"]) {
                    LOG_ERROR(where + ": pipeline '" + pname + "': Stufe '" + sname +
                              "' hat `run:` UND `steps:` — eine Stufe traegt einen Befehl "
                              "oder Schritte, nie beides (cosmos doc/spec/106 F106-11)");
                    ok = false;
                }
                ctor += "      - decl: [" + svar + ", stage, { call: [stage.new] }]\n";
                ctor += "      - call: [" + svar + ".set_name, \"" + yamlQuoted(sname) + "\"]\n";

                // ── Vorrang aufloesen: Stufe schlaegt Pipeline (F62-151) ────
                std::string stAgent = textAt(stg, "agent");
                if (stAgent.empty()) stAgent = headAgent;
                std::string stImage = textAt(stg, "image");
                if (stImage.empty()) stImage = headImage;
                std::string stBase = textAt(stg, "base");
                if (stBase.empty()) stBase = headBase;
                std::string stReq = csvOf(stg["requires"]);
                if (stReq.empty()) stReq = headRequires;
                const std::string stShares =
                    mergeShares(headShares, sharesOf(stg["shares"], where, pname, "stage", ok));
                int32_t stTimeout = numAt(stg, "timeout");
                if (stTimeout == 0) stTimeout = headTimeout;
                const YAML::Node stRes = stg["resources"];
                int32_t stCpu  = numAt(stRes, "cpu");
                if (stCpu == 0) stCpu = headCpu;
                int32_t stPids = numAt(stRes, "pids");
                if (stPids == 0) stPids = headPids;
                const std::string memText = textAt(stRes, "mem");
                int32_t stMem = memText.empty() ? headMem : memMb(memText, where, pname, ok);

                // `steplayer:` — OB je Stufe eine Schicht entsteht.  Die
                // Vorgabe ERGIBT sich aus `image:` und wird nicht geraten
                // (F110-125); widersprueechliche Angaben werden abgewiesen,
                // statt eine davon gewinnen zu lassen.
                std::string stLayer = textAt(stg, "steplayer");
                if (stLayer.empty()) stLayer = headLayer;
                if (!stLayer.empty() && stLayer != "own" && stLayer != "none") {
                    LOG_ERROR(where + ": pipeline '" + pname + "': Stufe '" + sname +
                              "': `steplayer: " + stLayer + "` — erlaubt sind `own` (eigene "
                              "Schicht, braucht `image:`) und `none` (auf dem Dateisystem des "
                              "Agenten) (spec/62 §18.5)");
                    ok = false;
                } else if (stLayer == "none" && !stImage.empty()) {
                    LOG_ERROR(where + ": pipeline '" + pname + "': Stufe '" + sname +
                              "': `steplayer: none` zusammen mit `image: " + stImage +
                              "` — ohne Schichten gibt es keine eigene Wurzel, und mit Abbild "
                              "gibt es Schichten (spec/62 §18.5 F110-125)");
                    ok = false;
                } else if (stLayer == "own" && stImage.empty()) {
                    LOG_ERROR(where + ": pipeline '" + pname + "': Stufe '" + sname +
                              "': `steplayer: own` ohne `image:` — eine eigene Schicht braucht "
                              "eine Wurzel, auf der sie liegt (spec/62 §18.5 F62-147)");
                    ok = false;
                } else if (stLayer.empty()) {
                    stLayer = stImage.empty() ? "none" : "own";
                }

                ctor += attrLine(svar, "agent",     stAgent);
                ctor += attrLine(svar, "steplayer", stLayer);
                ctor += attrLine(svar, "image",     stImage);
                ctor += attrLine(svar, "base",      stBase);
                ctor += attrLine(svar, "requires",  stReq);
                ctor += attrLine(svar, "shares",    stShares);
                ctor += attrLine(svar, "command",   textAt(stg, "run"));
                ctor += limitLine(svar, "cpu",     stCpu);
                ctor += limitLine(svar, "mem",     stMem);
                ctor += limitLine(svar, "pids",    stPids);
                ctor += limitLine(svar, "timeout", stTimeout);

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
                        if (cache != "pure" && cache != "resolved" && cache != "opaque") {
                            LOG_ERROR(where + ": pipeline '" + pname + "': Schritt '" + stepName +
                                      "': `cache: " + cache + "` — es gibt `pure`, `resolved` "
                                      "und `opaque` (spec/62 §7)");
                            ok = false;
                            continue;
                        }
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
                                // Die Achse erscheint im Rumpf mit ihrem
                                // DEKLARIERTEN Typ (F62-1): `jobs` ist ein
                                // i32, mit dem man rechnen kann.  Der Kanal
                                // traegt Text, also wird hier gewandelt — ein
                                // Mal, an einer Stelle.
                                const std::string axType = axisTypes.count(axis)
                                                               ? axisTypes[axis] : "string";
                                if (axType == "string") {
                                    prelude += "      - decl: [" + axis +
                                               ", string, { call: [step.arg, \"" + axis + "\"] }]\n";
                                } else {
                                    const std::string raw = "__ax_" + axis;
                                    prelude += "      - decl: [" + raw +
                                               ", string, { call: [step.arg, \"" + axis + "\"] }]\n";
                                    if (axType == "bool")
                                        prelude += "      - decl: [" + axis + ", bool, { eq: [" +
                                                   raw + ", \"true\"] }]\n";
                                    else if (axType == "f32")
                                        prelude += "      - decl: [" + axis + ", f32, { call: [" +
                                                   raw + ".parse_f32] }]\n";
                                    else
                                        prelude += "      - decl: [" + axis + ", " + axType +
                                                   ", { call: [" + raw + ".parse_i32] }]\n";
                                }
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
                        // Die Vorbedingungen der Stufe gelten vor ihrem ersten
                        // Schritt; danach waere die Haelfte der Arbeit schon
                        // getan, und die Meldung kaeme zu spaet (F62-126).
                        if (stepNo == 0 && !stageRequires.empty()) {
                            helpers += "      - decl: [__missing_env, string, { call: [sys.require_tools, \"" +
                                       stageRequires + "\"] }]\n";
                            helpers += "      - if:\n";
                            helpers += "          condition: { gt: [__missing_env.len, 0] }\n";
                            helpers += "          then:\n";
                            helpers += "            - call: [log.error, \"Stufe '" + sname +
                                       "': Umgebung unvollstaendig —${__missing_env} (requires:, spec/62 §18.2)\"]\n";
                            helpers += "            - throw: \"requires: Umgebung unvollstaendig —${__missing_env}\"\n";
                        }
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
        // `push:` erst jetzt: `from` nennt eine STUFE, und welche es gibt,
        // steht erst nach der Schleife fest (F62-71).
        for (const auto& one : pushes) {
            if (!declaredStages.count(one[0])) {
                LOG_ERROR(where + ": pipeline '" + pname + "': `push: { from: " + one[0] +
                          " }` nennt keine Stufe dieser Pipeline — ein Abbild ist ein "
                          "Dateisystem, und das gehoert einer Stufe (spec/62 F62-71)");
                ok = false;
                continue;
            }
            ctor += "      - call: [pipe_obj.add_push, \"" + yamlQuoted(one[0]) + "\", \"" +
                    yamlQuoted(one[1]) + "\", \"" + yamlQuoted(one[2]) + "\"]\n";
        }
        ctor += "      - return: pipe_obj\n";

        result += helpers;
        result += "- def:\n";
        result += "    name: " + fname + "\n";
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
