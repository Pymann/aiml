#ifndef AIML_PURITY_H
#define AIML_PURITY_H

// spec/62 §10.2 — purity is a property of a FUNCTION, not of a
// namespace: `yaml.parse` is pure, `yaml.parse_file` reads a file;
// `path.join` is pure, `path.home` depends on the host.
//
// Hence: a default per namespace, exceptions per function.  A namespace that
// is NOT listed here counts as impure (F62-18) — nothing becomes reusable
// by being left out.
#include <set>
#include <string>

namespace aiml {

inline bool namespaceDefaultPure(const std::string& ns) {
    static const std::set<std::string> kPure = {
        "math", "hash", "enums", "compress", "cascade",
        // spec/62 F62-112: `step` is PURE.  step.arg/step.host read exactly
        // what is in the key (the DECLARED axes and
        // host values, §8) — calling them impure would mean turning
        // key completeness against itself.
        // step.workspace is derived from lane and stage, so it is fixed
        // per lane; step.set_status/fail_lane/fail_pipeline say something
        // about the OUTCOME, not about the result (F62-92).
        "step"
    };
    return kPure.count(ns) > 0;
}

// The mixed namespaces, function by function (spec/62 §10.2).
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
        known = false;   // unknown -> impure, but as "unclassified"
        return false;
    }
    if (ns == "yaml") {
        // parse_file falls under the io rule (F62-19/F62-20), not here.
        if (fn == "parse_file") return false;
        return true;
    }
    known = false;
    return false;
}

inline bool isMixedNamespace(const std::string& ns) {
    return ns == "path" || ns == "yaml" || ns == "vocab" || ns == "tensor";
}

// Result of classifying a single call in a pure body.
enum class PurityVerdict {
    Allowed,        // pure, or log
    NeedsAssert,    // sys.exe — only with assert_pure (F62-22)
    IoConstOutside, // io with a constant absolute path (F62-19, error)
    IoComputed,     // io with a computed path (F62-20, warning)
    Impure          // everything else (F62-18, error)
};

inline PurityVerdict classifyCall(const std::string& ns, const std::string& fn,
                                  bool firstArgIsLiteral, const std::string& firstArgLiteral) {
    // log is always allowed: log output is not part of the result.
    if (ns == "log") return PurityVerdict::Allowed;

    // io cannot be forbidden — writing files is the point of a
    // build step.  Instead, the two cases from §10.2.
    if (ns == "io") {
        if (firstArgIsLiteral) {
            if (!firstArgLiteral.empty() && firstArgLiteral[0] == '/')
                return PurityVerdict::IoConstOutside;
            return PurityVerdict::Allowed;   // constant and relative: in the workspace
        }
        return PurityVerdict::IoComputed;
    }

    if (ns == "sys") {
        // The one permitted impure thing — with a signature (F62-22).
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
