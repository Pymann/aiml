#pragma once
// spec/65 §4 — Flusspruefung ueber die Aufrufkanten (F65-9), OPT-IN (F65-17).
//
// Nicht die Aufrufstelle allein entscheidet: ruft eine fuer alle offene Funktion
// intern eine eingeschraenkte, hat jeder Nutzer ueber den Umweg die
// eingeschraenkte Wirkung.  Darum die Kantenregel — und eine Diagnose, die den
// WEG nennt und nicht nur den Endpunkt (F65-11), weil man sonst am falschen
// Ende sucht.
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "../../logger.h"

namespace aiml {
namespace clearance {

// F65-17: laeuft nur auf ausdruecklichen Wunsch.  Der Normalfall ist ein Binary
// von einem Urheber (F65-1); dort hat die Regel nichts zu sagen.
inline bool flowCheckEnabled() {
    if (const char* v = std::getenv("AIML_STATIC_CLEARANCE")) {
        std::string s = v;
        return !(s.empty() || s == "0" || s == "off" || s == "OFF" ||
                 s == "false" || s == "FALSE");
    }
    return false;
}

// F65-5: ohne Annotation gilt Level 0 — das geschlossene Ende.
inline int effLevel(int level) { return level < 0 ? 0 : level; }

// Ein Knoten des Aufrufgraphen, wie der Compiler ihn schon fuehrt
// (FunctionInfo::referencedFuncs, spec/29_2 §16).
struct FlowNode {
    int level  = -1;                  // -1 = keine Annotation
    int grants = -1;                  // -1 = kein Gateway
    std::set<std::string> calls;      // gerufene benutzerdefinierte Funktionen
};

// Baut aus dem Graphen einen Aufruferpfad zu `target` zurueck, damit die
// Diagnose den Weg nennen kann (F65-11).  Erster gefundener Pfad genuegt: er
// zeigt, ueber welchen Einstieg das Privileg erreichbar ist.
inline std::string callerPath(const std::map<std::string, FlowNode>& g,
                              const std::string& target) {
    std::map<std::string, std::string> pred;
    std::set<std::string> seen{target};
    std::vector<std::string> frontier{target};
    std::string root = target;
    while (!frontier.empty()) {
        std::vector<std::string> next;
        for (const auto& cur : frontier) {
            bool hasCaller = false;
            for (const auto& [nm, node] : g) {
                if (seen.count(nm) || !node.calls.count(cur)) continue;
                pred[nm] = cur;
                seen.insert(nm);
                next.push_back(nm);
                hasCaller = true;
            }
            if (!hasCaller) root = cur;
        }
        if (next.empty()) break;
        root = next.front();
        frontier = std::move(next);
    }
    std::string path = root;
    for (std::string at = root; pred.count(at); at = pred[at])
        path += " -> " + pred[at];
    return path;
}

// F65-9.  Gibt die Zahl der Verstoesse zurueck; jeder ist ein harter Fehler,
// weil er ein Mangel der API-Oberflaeche des WIRTS ist, nicht des Skripts.
inline int checkFlow(const std::map<std::string, FlowNode>& graph) {
    int bad = 0;
    for (const auto& [from, node] : graph) {
        const int lvFrom = effLevel(node.level);
        // `grants` senkt die Schranke INNERHALB dieses Rumpfes (F65-10).
        const int floor = (node.grants >= 0 && node.grants < lvFrom) ? node.grants : lvFrom;
        for (const auto& to : node.calls) {
            auto it = graph.find(to);
            if (it == graph.end()) continue;          // unbekannt: nicht hier melden
            const int lvTo = effLevel(it->second.level);
            if (lvTo >= floor) continue;
            ++bad;
            LOG_ERROR("clearance-flow: `" + from + "` (level " + std::to_string(lvFrom) +
                      (node.grants >= 0 ? ", grants " + std::to_string(node.grants) : "") +
                      ") ruft `" + to + "` (level " + std::to_string(lvTo) +
                      ") — Weg: " + callerPath(graph, from) + " -> " + to +
                      ".  Entweder `grants: " + std::to_string(lvTo) + "` an `" + from +
                      "` setzen (bewusstes Gateway, spec/65 F65-10) oder die Kante aufloesen.");
        }
    }
    return bad;
}

}  // namespace clearance
}  // namespace aiml
