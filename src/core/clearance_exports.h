#pragma once
// spec/65 §5a — die Export-Tabelle des Wirts (F65-23/F65-24).
//
// EINE Tabelle, nicht zwei: je angebotenem Symbol eine Zeile mit Name, Level und
// (spaeter) Signatur und Adresse.  Zwei getrennte Listen — „was ist exportiert"
// neben „welches Level hat es" — waeren die Drift, die dieses Projekt schon
// mehrfach bezahlt hat.
//
// Diese Fassung traegt **Name und Level**.  Das genuegt fuer die beiden
// Pruefungen, die zur UEBERSETZUNGSZEIT stattfinden: die Zugriffspruefung
// (F65-4/F65-5) und die `extern`-Zulassung (F65-13).  Signatur und Adresse
// braucht erst das AUFRUFEN, und dafuer muss ein echter Wirt sie anmelden
// (F65-24, `DynamicLibrary::AddSymbol` + Funktionszeiger auf den eigenen `def`).
//
// Die Tabelle kommt vom WIRT, nicht aus der Quelle (F65-6).  `AIML_EXPORTS` nennt
// die Datei; wer sie setzt, ist der Wirt — dieselbe Begruendung wie bei
// `AIML_CLEARANCE`.  Das ist KEIN Widerspruch zu F65-32 („env darf nur
// verschaerfen"): dort geht es um die Grenzen der SITZUNG (clearance,
// create_level, replace_level).  Ein Eintrag hier ist das ANGEBOT des Wirts, und
// anzubieten ist sein Vorrecht.
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include "clearance.h"

namespace aiml {
namespace clearance {

// Nicht-konstant, weil der Wirt zur Laufzeit Zeilen HINZUFUEGT (F65-37).  Die Datei aus
// `AIML_EXPORTS` wird einmal eingelesen, danach ist die Tabelle das Gedaechtnis des Prozesses.
inline std::map<std::string, int>& exportTable() {
    static std::map<std::string, int> table;
    static bool loaded = false;
    if (loaded) return table;
    loaded = true;
    const char* file = std::getenv("AIML_EXPORTS");
    if (!file || !*file) return table;
    std::ifstream in(file);
    if (!in) {
        LOG_WARNING("AIML_EXPORTS: Datei nicht lesbar: " + std::string(file));
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

// Level des angebotenen Symbols, oder kAbsent, wenn der Wirt es nicht anbietet.
// F65-5: Auslassen gewaehrt nie — „nicht angeboten" ist nicht „offen".
inline int exportedLevel(const std::string& name) {
    auto& table = exportTable();
    auto it = table.find(name);
    return it == table.end() ? kAbsent : it->second;
}

// F65-37: der Wirt traegt eine Zeile ein (Name + Level).  Die Adresse hat der Aufrufer
// schon per `AddSymbol` angemeldet; hier kommt die Richtlinie dazu, damit beides aus
// EINEM Aufruf stammt und nicht driften kann (F65-23).
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
