#pragma once
// spec/65 §4 — flow check over the call edges (F65-9), OPT-IN (F65-17).
//
// The call site alone does not decide: if a function open to everyone internally
// calls a restricted one, every user has the restricted effect via the
// detour.  Hence the edge rule — and a diagnostic that names the
// PATH and not just the end point (F65-11), because otherwise one searches at the wrong
// end.
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "../../logger.h"

namespace aiml {
namespace clearance {

// F65-17: runs only on explicit request.  The normal case is a binary
// from one author (F65-1); there the rule has nothing to say.
inline bool flowCheckEnabled() {
    if (const char* v = std::getenv("AIML_STATIC_CLEARANCE")) {
        std::string s = v;
        return !(s.empty() || s == "0" || s == "off" || s == "OFF" ||
                 s == "false" || s == "FALSE");
    }
    return false;
}

// F65-5: without an annotation level 0 applies — the closed end.
inline int effLevel(int level) { return level < 0 ? 0 : level; }

// A node of the call graph, as the compiler already keeps it
// (FunctionInfo::referencedFuncs, spec/29_2 §16).
struct FlowNode {
    int level  = -1;                  // -1 = no annotation
    int grants = -1;                  // -1 = no gateway
    std::set<std::string> calls;      // gerufene benutzerdefinierte Funktionen
};

// Builds a caller path back to `target` from the graph, so that the
// diagnostic can name the path (F65-11).  The first path found is enough: it
// shows via which entry point the privilege is reachable.
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

// F65-9.  Returns the number of violations; each one is a hard error,
// because it is a defect of the HOST's API surface, not of the script.
inline int checkFlow(const std::map<std::string, FlowNode>& graph) {
    int bad = 0;
    for (const auto& [from, node] : graph) {
        const int lvFrom = effLevel(node.level);
        // `grants` lowers the barrier WITHIN this body (F65-10).
        const int floor = (node.grants >= 0 && node.grants < lvFrom) ? node.grants : lvFrom;
        for (const auto& to : node.calls) {
            auto it = graph.find(to);
            if (it == graph.end()) continue;          // unknown: do not report here
            const int lvTo = effLevel(it->second.level);
            if (lvTo >= floor) continue;
            ++bad;
            LOG_ERROR("clearance-flow: `" + from + "` (level " + std::to_string(lvFrom) +
                      (node.grants >= 0 ? ", grants " + std::to_string(node.grants) : "") +
                      ") calls `" + to + "` (level " + std::to_string(lvTo) +
                      ") — path: " + callerPath(graph, from) + " -> " + to +
                      ".  Either set `grants: " + std::to_string(lvTo) + "` an `" + from +
                      "` (a deliberate gateway, spec/65 F65-10) or resolve the edge.");
        }
    }
    return bad;
}

}  // namespace clearance
}  // namespace aiml
