#pragma once
// spec/65 — `level`/`grants` werden in JEDEM Build geprueft (F65-17: eine Annotation
// wird nie still ignoriert, sonst verrottet sie).  Durchsetzung gegen WIRTS-Symbole
// braucht eine Export-Tabelle (§5a) und steht noch aus; was hier schon wirkt, ist die
// Sitzung selbst und die Selbstbeglaubigung.
//
// Eine Stelle, eine Wahrheit: jede Deklarationsform ruft dieselben Pruefer, damit die
// Diagnose-Kennungen aus spec/65 §10 (F65-21) nicht auseinanderlaufen.
#include <cstdlib>
#include <string>
#include "../../logger.h"

namespace aiml {
namespace clearance {

constexpr int kAbsent = -1;    // keine Annotation — NICHT dasselbe wie Level 0
constexpr int kMax    = 255;

// F65-20/F65-22: die Freigabe der Sitzung.  Der WIRT setzt sie; `AIML_CLEARANCE` ist
// der Hebel fuer Proben.  Rueckgabe 0 heisst "keine Sitzung" = vertrauenswuerdig
// (F65-1/F65-7) und NICHT "Freigabe 0": 0 ist als Freigabe ungueltig, weil es der Wert
// eines vergessenen Feldes ist und das nie "Admin" heissen darf (F65-22).
inline int sessionClearance() {
    static int cached = -2;                 // -2 = noch nicht gelesen
    if (cached != -2) return cached;
    cached = 0;
    const char* v = std::getenv("AIML_CLEARANCE");
    if (!v || !*v) return cached;           // vertrauenswuerdiger Normalfall
    std::string s = v;
    size_t pos = 0;
    long val = 0;
    try { val = std::stol(s, &pos); } catch (...) { pos = 0; }
    bool ok = (pos == s.size()) && (val >= 1) && (val <= kMax);
    if (!ok) {
        LOG_ERROR("invalid-clearance: AIML_CLEARANCE=" + s +
                  " — gueltig ist 1..255.  0 ist der Wert eines vergessenen Feldes und "
                  "darf nie Admin bedeuten (spec/65 F65-22)");
        return cached;
    }
    cached = static_cast<int>(val);
    return cached;
}

inline bool restricted() { return sessionClearance() >= 1; }

// Reine SYNTAXpruefung: Ganzzahl 0..255, ohne jede Sitzungs-Richtlinie.
//
// Warum getrennt von `parseLevel`: dieselbe Zeichenkette kommt aus zwei ganz
// verschiedenen Quellen.  Aus der QUELLE eines Skripts ist ein `level:` in einer
// eingeschraenkten Sitzung Selbstbeglaubigung (F65-27) — aus der EXPORT-TABELLE
// des Wirts ist dasselbe Feld die Richtlinie selbst (§5a).  Eine Funktion mit
// beiden Bedeutungen hat genau deshalb die Tabelle des Wirts als
// Selbstbeglaubigung abgelehnt (gemessen 17.10.2026, `08_extern_admitted`).
// Wer die beiden wieder zusammenlegt, baut diesen Fehler erneut ein.
inline bool parseLevelSyntax(const std::string& text, const std::string& where, int& out) {
    if (text.empty()) {
        LOG_ERROR("invalid-level: leeres `level:` bei " + where +
                  " (spec/65 F65-4: Ganzzahl 0..255)");
        return false;
    }
    size_t pos = 0;
    long val = 0;
    try { val = std::stol(text, &pos); } catch (...) { pos = 0; }
    if (pos != text.size()) {
        LOG_ERROR("invalid-level: `level: " + text + "` bei " + where +
                  " ist keine Ganzzahl (spec/65 F65-4: 0..255)");
        return false;
    }
    if (val < 0 || val > kMax) {
        LOG_ERROR("invalid-level: `level: " + text + "` bei " + where +
                  " liegt ausserhalb 0..255 (spec/65 F65-4)");
        return false;
    }
    out = static_cast<int>(val);
    return true;
}

// F65-21 `invalid-level`: Ganzzahl 0..255 — fuer Annotationen in der QUELLE.
inline bool parseLevel(const std::string& text, const std::string& where, int& out) {
    // F65-27/F65-6: in einer eingeschraenkten Sitzung IST das Level eines angelegten
    // Symbols die Freigabe der Sitzung.  Die Quelle darf es nicht selbst setzen —
    // sonst legt sich ein Skript sein Privileg in einer Zeile selbst zu.
    if (restricted()) {
        LOG_ERROR("clearance-self-certify: `level:` bei " + where +
                  " — das Level eines angelegten Symbols IST die Freigabe der Sitzung; "
                  "Richtlinie gehoert dem Wirt (spec/65 F65-27)");
        return false;
    }
    return parseLevelSyntax(text, where, out);
}

// F65-21 `invalid-grants`: Syntax.  Eigene Kennung, damit die Diagnose die Ursache nennt.
inline bool parseGrants(const std::string& text, const std::string& where, int& out) {
    if (restricted()) {
        LOG_ERROR("clearance-self-certify: `grants:` bei " + where +
                  " — ein Gateway ist Richtlinie des Wirts (spec/65 F65-6/F65-27)");
        return false;
    }
    size_t pos = 0;
    long val = 0;
    if (text.empty()) {
        pos = 1;                            // erzwingt den Fehlerzweig
    } else {
        try { val = std::stol(text, &pos); } catch (...) { pos = 0; }
    }
    bool bad = text.empty() || (pos != text.size()) || val < 0 || val > kMax;
    if (bad) {
        LOG_ERROR("invalid-grants: `grants: " + text + "` bei " + where +
                  " ist keine Ganzzahl 0..255 (spec/65 F65-10)");
        return false;
    }
    out = static_cast<int>(val);
    return true;
}

// F65-21 `invalid-grants`: braucht ein `level:` und darf es nicht UEBERSCHREITEN —
// ein Gateway darf Privilegien nur fallen lassen (spec/65 F65-10).
inline bool checkGrants(int level, int grants, const std::string& where) {
    if (grants == kAbsent) return true;
    if (level == kAbsent) {
        LOG_ERROR("invalid-grants: `grants:` ohne `level:` bei " + where +
                  " (spec/65 F65-10)");
        return false;
    }
    if (grants > level) {
        LOG_ERROR("invalid-grants: `grants: " + std::to_string(grants) +
                  "` ueber `level: " + std::to_string(level) + "` bei " + where +
                  " — ein Gateway darf Privilegien nur fallen lassen (spec/65 F65-10)");
        return false;
    }
    return true;
}

}  // namespace clearance
}  // namespace aiml
