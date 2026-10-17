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
// Der Speicher der aktuellen Freigabe.  -2 heisst "noch nicht aus der Umgebung gelesen".
//
// KEIN einmaliger Zwischenspeicher mehr (F65-36): ein Wirt fuehrt Skripte fuer verschiedene
// Nutzer aus, und eine Freigabe, die einmal gelesen und gemerkt wird, ist keine Eigenschaft
// einer SITZUNG, sondern des Prozesses.  `sys.exec` setzt sie fuer die Dauer eines Laufs und
// stellt sie danach zurueck.
inline int& clearanceSlot() {
    static int slot = -2;
    return slot;
}

inline int sessionClearance() {
    int& slot = clearanceSlot();
    if (slot != -2) return slot;
    slot = 0;
    const char* v = std::getenv("AIML_CLEARANCE");
    if (!v || !*v) return slot;           // vertrauenswuerdiger Normalfall
    std::string s = v;
    size_t pos = 0;
    long val = 0;
    try { val = std::stol(s, &pos); } catch (...) { pos = 0; }
    bool ok = (pos == s.size()) && (val >= 1) && (val <= kMax);
    if (!ok) {
        LOG_ERROR("invalid-clearance: AIML_CLEARANCE=" + s +
                  " — gueltig ist 1..255.  0 ist der Wert eines vergessenen Feldes und "
                  "darf nie Admin bedeuten (spec/65 F65-22)");
        return slot;
    }
    slot = static_cast<int>(val);
    return slot;
}

// F65-36: Freigabe fuer EINE Sitzung setzen.  Darf nur VERSCHAERFEN (hoehere Zahl =
// weniger Rechte), damit ein Aufruf sich nicht mehr nehmen kann als die Prozess-Vorgabe.
// Rueckgabe ist die vorige Freigabe, damit der Aufrufer sie zurueckstellen kann.
inline int pushSessionClearance(int requested) {
    const int before = sessionClearance();
    if (requested >= 1 && requested <= kMax) {
        int& slot = clearanceSlot();
        slot = (before >= 1 && before > requested) ? before : requested;
    }
    return before;
}

inline void popSessionClearance(int previous) {
    clearanceSlot() = previous;
}

inline bool restricted() { return sessionClearance() >= 1; }

// ── §5b: die SCHREIBSEITE der Richtlinie (F65-26) ──────────────────────────────
//
// `create_level` und `replace_level` werden GEGENLAEUFIG verglichen: erlaubt ist
// `clearance <= create_level`, also ist die KLEINERE Zahl die strengere und 0 heisst
// niemand (F65-22).  Darum ist "nicht gesetzt" hier NICHT 0, sondern "keine Vorgabe":
// waere es 0, koennte die Umgebung das Recht nur abschalten und nie erteilen, weil die
// Verschaerfungsregel aus F65-32 dann jeden Wert am Minimum festhielte.
constexpr int kPolicyUnset = -1;

inline int readEnvPolicy(const char* var) {
    const char* v = std::getenv(var);
    if (!v || !*v) return kPolicyUnset;
    std::string s = v;
    size_t pos = 0;
    long val = 0;
    try { val = std::stol(s, &pos); } catch (...) { pos = 0; }
    if (pos != s.size() || val < 0 || val > kMax) {
        LOG_ERROR("invalid-level: " + std::string(var) + "=" + s +
                  " — gueltig ist 0..255 (spec/65 F65-26)");
        return kPolicyUnset;
    }
    return static_cast<int>(val);
}

inline int envCreateLevel() {
    static int v = readEnvPolicy("AIML_CREATE_LEVEL");
    return v;
}
inline int envReplaceLevel() {
    static int v = readEnvPolicy("AIML_REPLACE_LEVEL");
    return v;
}

inline int& createSlot() {
    static int slot = (envCreateLevel() == kPolicyUnset) ? 0 : envCreateLevel();
    return slot;
}
inline int& replaceSlot() {
    static int slot = (envReplaceLevel() == kPolicyUnset) ? 0 : envReplaceLevel();
    return slot;
}

inline int createLevel()  { return createSlot(); }
inline int replaceLevel() { return replaceSlot(); }

// F65-26: die drei Werte reisen zusammen, weil sie drei Antworten auf EINE Frage sind.
struct Policy { int clearance; int create; int replace; };

// F65-32: die Umgebung ist die Vorgabe des Prozesses und darf nur VERSCHAERFEN —
// bei den Schreibrechten also nach unten.  Ohne Vorgabe gilt der Wunsch der Sitzung.
inline int tightenWrite(int envDefault, int requested) {
    if (requested < 0 || requested > kMax) requested = 0;
    return (envDefault == kPolicyUnset) ? requested
                                        : (envDefault < requested ? envDefault : requested);
}

inline Policy pushSessionPolicy(int clearance, int create, int replace) {
    Policy before{ sessionClearance(), createSlot(), replaceSlot() };
    pushSessionClearance(clearance);
    createSlot()  = tightenWrite(envCreateLevel(),  create);
    replaceSlot() = tightenWrite(envReplaceLevel(), replace);
    return before;
}

inline void popSessionPolicy(const Policy& before) {
    clearanceSlot() = before.clearance;
    createSlot()    = before.create;
    replaceSlot()   = before.replace;
}

// Darf diese Sitzung in die Tabelle des Wirts schreiben?  `restricted()` davor: ohne
// Sitzung gibt es keine Grenze und damit nichts zu pruefen (F65-1).
inline bool mayCreate()  { return !restricted() || sessionClearance() <= createLevel(); }
inline bool mayReplace() { return !restricted() || sessionClearance() <= replaceLevel(); }

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
