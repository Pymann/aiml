#pragma once
// spec/65 §5a — the host's export table (F65-23/F65-24).
//
// ONE table, not two: per offered symbol one row with name, level and
// (later) signature and address.  Two separate lists — "what is exported"
// next to "which level does it have" — would be the drift this project has already
// paid for several times.
//
// This version carries **name and level**.  That is enough for the two
// checks that take place at COMPILE TIME: the access check
// (F65-4/F65-5) and the `extern` admission (F65-13).  Signature and address
// are only needed for CALLING, and for that a real host must register them
// (F65-24, `DynamicLibrary::AddSymbol` + function pointer to its own `def`).
//
// The table comes from the HOST, not from the source (F65-6).  `AIML_EXPORTS` names
// the file; whoever sets it is the host — the same reasoning as for
// `AIML_CLEARANCE`.  That is NO contradiction to F65-32 ("env may only
// tighten"): that is about the limits of the SESSION (clearance,
// create_level, replace_level).  An entry here is the host's OFFER, and
// offering is its prerogative.
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include "clearance.h"

namespace aiml {
namespace clearance {

// Non-constant, because the host ADDS rows at runtime (F65-37).  The file from
// `AIML_EXPORTS` is read once, after that the table is the memory of the process.
inline std::map<std::string, int>& exportTable() {
    static std::map<std::string, int> table;
    static bool loaded = false;
    if (loaded) return table;
    loaded = true;
    const char* file = std::getenv("AIML_EXPORTS");
    if (!file || !*file) return table;
    std::ifstream in(file);
    if (!in) {
        LOG_WARNING("AIML_EXPORTS: file not readable: " + std::string(file));
        return table;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const size_t bar = line.find('|');
        if (bar == std::string::npos) continue;
        const std::string name = line.substr(0, bar);
        std::string rest = line.substr(bar + 1);
        const size_t bar2 = rest.find('|');
        if (bar2 != std::string::npos) rest = rest.substr(0, bar2);
        int level = kAbsent;
        if (!parseLevelSyntax(rest, "AIML_EXPORTS " + name, level)) continue;
        if (!name.empty()) table[name] = level;
    }
    return table;
}

// Level of the offered symbol, or kAbsent if the host does not offer it.
// F65-5: omission never grants — "not offered" is not "open".
inline int exportedLevel(const std::string& name) {
    auto& table = exportTable();
    auto it = table.find(name);
    return it == table.end() ? kAbsent : it->second;
}

// F65-37: the host enters a row (name + level).  The caller has already registered the
// address via `AddSymbol`; here the policy is added, so that both come from
// ONE call and cannot drift (F65-23).
inline void registerExport(const std::string& name, int level) {
    exportTable()[name] = level;
}

inline bool exportReachable(const std::string& name) {
    const int level = exportedLevel(name);
    if (level == kAbsent) return false;
    return sessionClearance() <= level;
}

}  // namespace clearance
}  // namespace aiml
