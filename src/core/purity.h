#ifndef AIML_PURITY_H
#define AIML_PURITY_H

// spec/62 §10.2 — Reinheit ist Eigenschaft einer FUNKTION, nicht eines
// Namensraums: `yaml.parse` ist rein, `yaml.parse_file` liest eine Datei;
// `path.join` ist rein, `path.home` haengt am Wirt.
//
// Darum: Vorgabe je Namensraum, Ausnahmen je Funktion.  Ein Namensraum, der
// hier NICHT steht, gilt als unrein (F62-18) — nichts wird durch Weglassen
// wiederverwendbar.
#include <set>
#include <string>

namespace aiml {

inline bool namespaceDefaultPure(const std::string& ns) {
    static const std::set<std::string> kPure = {
        "math", "hash", "enums", "compress", "cascade",
        // spec/62 F62-112: `step` ist REIN.  step.arg/step.host lesen genau
        // das, was im Schluessel steht (die DEKLARIERTEN Achsen und
        // Wirtswerte, §8) — sie unrein zu nennen hiesse, die
        // Schluesselvollstaendigkeit gegen sich selbst zu wenden.
        // step.workspace ist aus Lane und Stufe abgeleitet, also je Lane
        // festgelegt; step.set_status/fail_lane/fail_pipeline sagen etwas
        // ueber den AUSGANG, nicht ueber das Ergebnis (F62-92).
        "step"
    };
    return kPure.count(ns) > 0;
}

// Die gemischten Namensraeume, Funktion fuer Funktion (spec/62 §10.2).
inline bool mixedNamespacePure(const std::string& ns, const std::string& fn, bool& known) {
    known = true;
    if (ns == "path") {
        static const std::set<std::string> kPure = {
            "from_string", "join", "safe_join", "empty", "ends_with"
        };
        static const std::set<std::string> kImpure = {
            "home", "cwd", "temp_dir", "current_exe", "config_dir"
        };
        if (kPure.count(fn))   return true;
        if (kImpure.count(fn)) return false;
        known = false;   // unbekannt -> unrein, aber als "unklassifiziert"
        return false;
    }
    if (ns == "yaml") {
        // parse_file faellt unter die io-Regel (F62-19/F62-20), nicht hierher.
        if (fn == "parse_file") return false;
        return true;
    }
    known = false;
    return false;
}

inline bool isMixedNamespace(const std::string& ns) {
    return ns == "path" || ns == "yaml" || ns == "vocab" || ns == "tensor";
}

// Ergebnis der Einordnung eines einzelnen Aufrufs in einem reinen Rumpf.
enum class PurityVerdict {
    Allowed,        // rein, oder log
    NeedsAssert,    // sys.exe — nur mit assert_pure (F62-22)
    IoConstOutside, // io mit konstantem absolutem Pfad (F62-19, Fehler)
    IoComputed,     // io mit berechnetem Pfad (F62-20, Warnung)
    Impure          // alles andere (F62-18, Fehler)
};

inline PurityVerdict classifyCall(const std::string& ns, const std::string& fn,
                                  bool firstArgIsLiteral, const std::string& firstArgLiteral) {
    // log ist immer erlaubt: Protokollausgabe ist nicht Teil des Ergebnisses.
    if (ns == "log") return PurityVerdict::Allowed;

    // io kann nicht verboten werden — Dateien zu schreiben ist der Sinn eines
    // Build-Schritts.  Stattdessen die beiden Faelle aus §10.2.
    if (ns == "io") {
        if (firstArgIsLiteral) {
            if (!firstArgLiteral.empty() && firstArgLiteral[0] == '/')
                return PurityVerdict::IoConstOutside;
            return PurityVerdict::Allowed;   // konstant und relativ: im Arbeitsbereich
        }
        return PurityVerdict::IoComputed;
    }

    if (ns == "sys") {
        // Das eine erlaubte unreine Ding — mit Unterschrift (F62-22).
        if (fn == "exe") return PurityVerdict::NeedsAssert;
        return PurityVerdict::Impure;
    }

    if (isMixedNamespace(ns)) {
        bool known = false;
        bool pure  = mixedNamespacePure(ns, fn, known);
        return pure ? PurityVerdict::Allowed : PurityVerdict::Impure;
    }

    return namespaceDefaultPure(ns) ? PurityVerdict::Allowed : PurityVerdict::Impure;
}

} // namespace aiml
#endif
