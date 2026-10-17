#pragma once
// spec/65 F65-35 — Builtins sind von Grund auf ZU.
//
// Jeder Namensraum ist Level 0, solange diese Tabelle ihn nicht ausdruecklich
// hebt.  Ein frueherer Entwurf listete `sys`, `db`, `io`, `net` als die
// gefaehrlichen — die falsche Form: so eine Liste ist nur bis zum naechsten
// Builtin vollstaendig.  Die Vorgabe schliesst, das Oeffnen ist der bewusste Akt.
//
// Form wie `purity.h`: Vorgabe je Namensraum, Ausnahmen je Funktion.
#include <set>
#include <string>
#include "clearance.h"

namespace aiml {
namespace clearance {

// Was ein Wirt ueblicherweise oeffnet: die reinen Namensraeume (keine Wirkung auf
// die Aussenwelt, vgl. purity.h) plus `log`, damit ein Skript ueberhaupt etwas
// sagen kann — ohne Ausgabe ist eine Konsole keine.
inline int builtinLevel(const std::string& ns, const std::string& fn) {
    static const std::set<std::string> kOpenNs = {
        "math", "hash", "enums", "compress", "cascade", "log"
    };
    // F65-32: die Richtlinie ist LESBAR, nie schreibbar.  Der Namensraum bleibt
    // darum ZU und nur die drei Leser sind offen — so nennt ein Griff nach einem
    // Setzer den Grund (`clearance-builtin`), statt wie ein Tippfehler auszusehen.
    if (ns == "clearance")
        return (fn == "level" || fn == "create_level" || fn == "replace_level") ? kMax : 0;
    if (kOpenNs.count(ns)) return kMax;
    return 0;
}

// Erreichbar fuer diese Sitzung?  Ausserhalb einer eingeschraenkten Sitzung
// immer (F65-1: im Normalfall existiert das Merkmal nicht).
inline bool builtinReachable(const std::string& ns, const std::string& fn) {
    if (!restricted()) return true;
    return sessionClearance() <= builtinLevel(ns, fn);
}

}  // namespace clearance
}  // namespace aiml
