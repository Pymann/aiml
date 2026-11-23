#pragma once
// spec/65 — `level`/`grants` are checked in EVERY build (F65-17: an annotation
// is never silently ignored, otherwise it rots).  Enforcement against HOST symbols
// needs an export table (§5a) and is still pending; what already takes effect here is the
// session itself and self-certification.
//
// One place, one truth: every declaration form calls the same checkers, so that the
// diagnostic identifiers from spec/65 §10 (F65-21) do not drift apart.
#include <cstdlib>
#include <string>
#include "../../logger.h"

namespace aiml {
namespace clearance {

constexpr int kAbsent = -1;    // no annotation — NOT the same as level 0
constexpr int kMax    = 255;

// F65-20/F65-22: the session's clearance.  The HOST sets it; `AIML_CLEARANCE` is
// the lever for probes.  A return of 0 means "no session" = trusted
// (F65-1/F65-7) and NOT "clearance 0": 0 is invalid as a clearance, because it is the value
// of a forgotten field and that must never mean "admin" (F65-22).
// The storage of the current clearance.  -2 means "not yet read from the environment".
//
// NO one-time cache any more (F65-36): a host runs scripts for different
// users, and a clearance that is read once and remembered is not a property
// of a SESSION but of the process.  `sys.exec` sets it for the duration of a run and
// restores it afterwards.
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
                  " — valid is 1..255.  0 is the value of a forgotten field and "
                  "must never mean admin (spec/65 F65-22)");
        return slot;
    }
    slot = static_cast<int>(val);
    return slot;
}

// F65-36: set the clearance for ONE session.  May only TIGHTEN (higher number =
// fewer rights), so that a call cannot take more than the process default.
// The return value is the previous clearance, so that the caller can restore it.
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

// ── §5b: the WRITE SIDE of the policy (F65-26) ────────────────────────────────
//
// `create_level` and `replace_level` are compared the OTHER WAY ROUND: allowed is
// `clearance <= create_level`, so the SMALLER number is the stricter one and 0 means
// nobody (F65-22).  That is why "not set" is NOT 0 here, but "no default":
// if it were 0, the environment could only switch the right off and never grant it, because the
// tightening rule from F65-32 would then pin every value at the minimum.
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
                  " — valid is 0..255 (spec/65 F65-26)");
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

// F65-26: the three values travel together, because they are three answers to ONE question.
struct Policy { int clearance; int create; int replace; };

// F65-32: the environment is the process default and may only TIGHTEN —
// for the write rights that means downwards.  Without a default the session's wish applies.
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

// May this session write into the host's table?  `restricted()` first: without
// a session there is no boundary and therefore nothing to check (F65-1).
inline bool mayCreate()  { return !restricted() || sessionClearance() <= createLevel(); }
inline bool mayReplace() { return !restricted() || sessionClearance() <= replaceLevel(); }

// Pure SYNTAX check: integer 0..255, without any session policy.
//
// Why separate from `parseLevel`: the same string comes from two entirely
// different sources.  From a script's SOURCE a `level:` in a
// restricted session is self-certification (F65-27) — from the host's EXPORT TABLE
// the same field is the policy itself (§5a).  A function with
// both meanings rejected the host's table as
// self-certification for exactly that reason (measured 2026-10-17, `08_extern_admitted`).
// Whoever merges the two again builds this bug back in.
inline bool parseLevelSyntax(const std::string& text, const std::string& where, int& out) {
    if (text.empty()) {
        LOG_ERROR("invalid-level: empty `level:` at " + where +
                  " (spec/65 F65-4: integer 0..255)");
        return false;
    }
    size_t pos = 0;
    long val = 0;
    try { val = std::stol(text, &pos); } catch (...) { pos = 0; }
    if (pos != text.size()) {
        LOG_ERROR("invalid-level: `level: " + text + "` at " + where +
                  " is not an integer (spec/65 F65-4: 0..255)");
        return false;
    }
    if (val < 0 || val > kMax) {
        LOG_ERROR("invalid-level: `level: " + text + "` at " + where +
                  " lies outside 0..255 (spec/65 F65-4)");
        return false;
    }
    out = static_cast<int>(val);
    return true;
}

// F65-21 `invalid-level`: integer 0..255 — for annotations in the SOURCE.
inline bool parseLevel(const std::string& text, const std::string& where, int& out) {
    // F65-27/F65-6: in a restricted session the level of a created
    // symbol IS the session's clearance.  The source may not set it itself —
    // otherwise a script grants itself its privilege in one line.
    if (restricted()) {
        LOG_ERROR("clearance-self-certify: `level:` at " + where +
                  " — the level of a created symbol IS the session's clearance; "
                  "policy belongs to the host (spec/65 F65-27)");
        return false;
    }
    return parseLevelSyntax(text, where, out);
}

// F65-21 `invalid-grants`: syntax.  An identifier of its own, so that the diagnostic names the cause.
inline bool parseGrants(const std::string& text, const std::string& where, int& out) {
    if (restricted()) {
        LOG_ERROR("clearance-self-certify: `grants:` at " + where +
                  " — a gateway is the host's policy (spec/65 F65-6/F65-27)");
        return false;
    }
    size_t pos = 0;
    long val = 0;
    if (text.empty()) {
        pos = 1;                            // forces the error branch
    } else {
        try { val = std::stol(text, &pos); } catch (...) { pos = 0; }
    }
    bool bad = text.empty() || (pos != text.size()) || val < 0 || val > kMax;
    if (bad) {
        LOG_ERROR("invalid-grants: `grants: " + text + "` at " + where +
                  " is not an integer 0..255 (spec/65 F65-10)");
        return false;
    }
    out = static_cast<int>(val);
    return true;
}

// F65-21 `invalid-grants`: needs a `level:` and must not EXCEED it —
// a gateway may only drop privileges (spec/65 F65-10).
inline bool checkGrants(int level, int grants, const std::string& where) {
    if (grants == kAbsent) return true;
    if (level == kAbsent) {
        LOG_ERROR("invalid-grants: `grants:` without `level:` at " + where +
                  " (spec/65 F65-10)");
        return false;
    }
    if (grants > level) {
        LOG_ERROR("invalid-grants: `grants: " + std::to_string(grants) +
                  "` above `level: " + std::to_string(level) + "` at " + where +
                  " — a gateway may only drop privileges (spec/65 F65-10)");
        return false;
    }
    return true;
}

}  // namespace clearance
}  // namespace aiml
