#pragma once
// spec/65 F65-35 — builtins are CLOSED from the ground up.
//
// Every namespace is level 0 as long as this table does not explicitly
// raise it.  An earlier draft listed `sys`, `db`, `io`, `net` as the
// dangerous ones — the wrong form: such a list is only complete until the next
// builtin.  The default closes, opening is the deliberate act.
//
// Form like `purity.h`: a default per namespace, exceptions per function.
#include <set>
#include <string>
#include "clearance.h"

namespace aiml {
namespace clearance {

// What a host usually opens: the pure namespaces (no effect on
// the outside world, cf. purity.h) plus `log`, so that a script can say anything
// at all — without output a console is not a console.
inline int builtinLevel(const std::string& ns, const std::string& fn) {
    static const std::set<std::string> kOpenNs = {
        "math", "hash", "enums", "compress", "cascade", "log"
    };
    // F65-32: the policy is READABLE, never writable.  That is why the namespace stays
    // CLOSED and only the three readers are open — that way reaching for a
    // setter names the reason (`clearance-builtin`) instead of looking like a typo.
    if (ns == "clearance")
        return (fn == "level" || fn == "create_level" || fn == "replace_level") ? kMax : 0;
    if (kOpenNs.count(ns)) return kMax;
    return 0;
}

// Reachable for this session?  Outside a restricted session
// always (F65-1: in the normal case the feature does not exist).
inline bool builtinReachable(const std::string& ns, const std::string& fn) {
    if (!restricted()) return true;
    return sessionClearance() <= builtinLevel(ns, fn);
}

}  // namespace clearance
}  // namespace aiml
