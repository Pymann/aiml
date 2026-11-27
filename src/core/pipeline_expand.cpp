// spec/62 §13.5f — declared pipelines into the composed form.
//
// Generated per step are a body function, optionally a probe
// function, and per pipeline a function <name>() with return type
// `pipeline` that composes everything (F62-108).
//
// The declared axes appear in the body as local variables of the same
// name (F62-109), read with step.arg — that is why one writes `${arch}` and
// means the axis.  A `${NAME}` that is neither an axis nor a host name
// thereby becomes an undefined variable, and exactly that is the
// check F62-7 demands (F62-110) — without a second check pass that
// could disagree with the first.
#include "pipeline_expand.h"
#include "yaml_dump.h"
#include "../../logger.h"

#include <algorithm>
#include <regex>
#include <cctype>
#include <array>
#include <map>
#include <set>
#include <cstdlib>
#include <sstream>
#include <vector>
#include <yaml-cpp/yaml.h>

namespace aiml {

namespace {

// Fields that the rewrite does NOT YET cover.  They are rejected and
// not ignored (F62-111): a silently discarded `needs:` would be
// a pipeline that runs in the wrong order and reports green while doing so.
// F62-111: what the rewrite rejects by name.  `outputs` belongs to the step that
// writes it (F62-168); on a stage it would not say which step that is.
const char* kUnsupportedStageFields[] = { "outputs" };

// Fields that require a RUNTIME (spec/62 §18.1 F62-124).  They are
// not called "not covered yet", because that would be the wrong information:
// the form is fixed, only this run cannot execute it.  A
// silent fallback to the developer's file system would be worse than
// a run that does not start at all — it would be called "built against ubuntu:24.04" and
// would not be.
struct RuntimeField { const char* name; const char* what; };
const RuntimeField kRuntimeFields[] = {
    { "image", "a root file system" },
    { "push",  "a place to which an image is written" },
};

// F62-33/F62-8: how the result of `need` appears in the body — a local name equal
// to the step name, with the producer's type.  Text and i32 as written; every other
// type in its serialised form (spec/29), read back with sys.deserialize.  `fanIn`:
// a `per: once` step needs a `per: lane` one and gets a list over the lanes.
std::string needBinding(const std::string& need, const std::string& type, bool fanIn) {
    auto convert = [&](const std::string& target, const std::string& text,
                       const std::string& indent) -> std::string {
        if (type == "string") return indent + "- decl: [" + target + ", string, " + text + "]\n";
        if (type == "i32")
            return indent + "- decl: [" + target + ", i32, { call: [" + text + ".parse_i32] }]\n";
        return indent + "- decl: [" + target + ", " + type + ", { call: [sys.deserialize, " + text +
               ", \"" + type + "\"] }]\n";
    };
    if (!fanIn) {
        if (type == "string")
            return "      - decl: [" + need + ", string, { call: [step.need_result, \"" + need + "\"] }]\n";
        std::string raw = "__nd_" + need;
        return "      - decl: [" + raw + ", string, { call: [step.need_result, \"" + need + "\"] }]\n" +
               convert(need, raw, "      ");
    }
    std::string texts = "__nl_" + need, idx = "__ni_" + need, one = "__ne_" + need;
    std::string out = "      - decl: [" + texts + ", list<string>, { call: [step.need_results, \"" + need + "\"] }]\n";
    if (type == "string") return out + "      - decl: [" + need + ", list<string>, " + texts + "]\n";
    out += "      - decl: [" + need + ", list<" + type + ">]\n";
    out += "      - decl: [" + idx + ", i32, 0]\n";
    out += "      - while:\n";
    out += "          condition: { lt: [" + idx + ", " + texts + ".len] }\n";
    out += "          body:\n";
    out += "            - decl: [" + one + "_txt, string, { at: [" + texts + ", " + idx + "] }]\n";
    if (type == "i32")
        out += "            - decl: [" + one + ", i32, { call: [" + one + "_txt.parse_i32] }]\n";
    else
        out += "            - decl: [" + one + ", " + type + ", { call: [sys.deserialize, " + one +
               "_txt, \"" + type + "\"] }]\n";
    out += "            - call: [" + need + ".push, " + one + "]\n";
    out += "            - set: [" + idx + ", { +: [" + idx + ", 1] }]\n";
    return out;
}

// How a field that needs a runtime is reported.
std::string runtimeFieldMessage(const std::string& where, const std::string& pname,
                                const char* field, const char* what, const char* level) {
    return where + ": pipeline '" + pname + "': " + level + " field '" + field +
           "' requires " + what + " — this run is LOCAL and has no runtime "
           "(spec/62 §18.1 F62-124).  Either leave the field out (then the "
           "pipeline runs against this machine, F62-125), or start the run through a host "
           "that can do containers (cosmos doc/spec/110).";
}

std::string indentBlock(const std::string& text, const std::string& pad) {
    std::string out = pad;
    for (char ch : text) {
        out += ch;
        if (ch == '\n') out += pad;
    }
    return out;
}

// A YAML subtree as flow text, so that it fits into a generated line.
std::string flowOf(const YAML::Node& node) {
    YAML::Emitter em;
    em << YAML::Flow;
    emitPreservingQuotes(em, node);
    const char* s = em.c_str();
    return s ? std::string(s) : std::string();
}

// The statements of a body as a block sequence, indented for `body:`.
std::string bodyBlock(const YAML::Node& seq, const std::string& pad) {
    std::string out;
    for (const auto& item : seq) {
        out += pad + "- " + flowOf(item) + "\n";
    }
    return out;
}

// spec/62 §5.4 (F62-27): the fields of ExeOptions — spec/23 §5.1's options, no more.
const char* kExeFields[] = {
    "cwd", "env", "shell", "timeout", "capture", "stdin", "stdout", "stderr", "detach", "status_file",
};

// F62-61/F62-58: `exec_defaults` field by field, pipeline → stage → step, innermost
// wins.  The values stay source text (a name like `workspace`, or a literal).
void mergeExecDefaults(const YAML::Node& node, std::vector<std::pair<std::string, std::string>>& into,
                       const std::string& where, const std::string& pname, const std::string& level,
                       bool& ok) {
    if (!node) return;
    if (!node.IsMap()) {
        LOG_ERROR(where + ": pipeline '" + pname + "': `exec_defaults:` on the " + level +
                  " is a map of ExeOptions fields (spec/62 F62-61)");
        ok = false;
        return;
    }
    for (auto it = node.begin(); it != node.end(); ++it) {
        const std::string key = it->first.Scalar();
        bool known = false;
        for (const char* f : kExeFields) if (key == f) known = true;
        if (!known) {
            LOG_ERROR(where + ": pipeline '" + pname + "': `exec_defaults:` on the " + level +
                      " names '" + key + "', which is no field of ExeOptions (spec/62 F62-27)");
            ok = false;
            continue;
        }
        std::string val = it->second.IsScalar() ? it->second.Scalar() : flowOf(it->second);
        if (it->second.IsScalar() && (key == "cwd" || key == "stdin" || key == "stdout" ||
                                      key == "stderr" || key == "status_file") &&
            it->second.Tag() == "!")             // a quoted literal stays a literal
            val = "\"" + val + "\"";
        bool replaced = false;
        for (auto& kv : into) if (kv.first == key) { kv.second = val; replaced = true; }
        if (!replaced) into.emplace_back(key, val);
    }
}

// F62-61: inside a step that has exec_defaults, `decl: [o, ExeOptions]` starts from
// them — the declaration gets the defaults as its initial value.  Nested blocks too:
// they are the same scope for this purpose.
void seedExeOptionDecls(YAML::Node node) {
    if (node.IsSequence()) {
        for (auto child : node) seedExeOptionDecls(child);
        return;
    }
    if (!node.IsMap()) return;
    for (auto it = node.begin(); it != node.end(); ++it) {
        YAML::Node val = it->second;
        if (it->first.Scalar() == "decl" && val.IsSequence() && val.size() == 2 &&
            val[1].IsScalar() && val[1].Scalar() == "ExeOptions") {
            val.push_back("__exec_defaults");
            continue;
        }
        seedExeOptionDecls(val);
    }
}

// F62-31: how a `sys.exe` in a body passes `env`.  0 = it does, 1 = it does not,
// 2 = through a value whose fields the source does not show.  The worst over the body.
int exeEnvVerdict(const YAML::Node& node, bool defaultsHaveEnv) {
    int worst = 0;
    if (node.IsSequence()) {
        for (const auto& c : node) worst = std::max(worst, exeEnvVerdict(c, defaultsHaveEnv));
        return worst;
    }
    if (!node.IsMap()) return 0;
    for (auto it = node.begin(); it != node.end(); ++it) {
        const YAML::Node& val = it->second;
        if (it->first.Scalar() == "call" && val.IsSequence() && val.size() >= 2 &&
            val[0].IsScalar() && val[0].Scalar() == "sys.exe") {
            int here = defaultsHaveEnv ? 0 : 1;
            for (size_t i = 2; i < val.size(); ++i) {
                if (val[i].IsMap()) { if (val[i]["env"]) here = 0; }
                else if (val[i].IsScalar() && here == 1) here = 2;   // a model value
            }
            worst = std::max(worst, here);
        }
        worst = std::max(worst, exeEnvVerdict(val, defaultsHaveEnv));
    }
    return worst;
}

std::string scalarOr(const YAML::Node& node, const std::string& fallback) {
    return (node && node.IsScalar()) ? node.Scalar() : fallback;
}

// ── spec/62 §18 + §20: the distributed keys ─────────────────────────────────
//
// They are COMPILED and not rejected (F62-124a): the same file is
// valid for a cluster, and a controller should read it instead of parsing it
// itself (§20.1).  Only the local RUN is rejected
// (`aiml_pipeline_local_refusal`).
//
// Old names are called by name (F110-127): two spellings for
// the same thing are the redundancy that nobody resolves later.
struct OldName { const char* old_key; const char* now; const char* why; };
const OldName kOldNames[] = {
    { "runs_on",   "agent",     "" },
    { "label",     "agent",     "" },
    { "runs_in",   "steplayer", "" },
    // Not just a different name, but a different UNIT too: `timeout:` counts
    // in MILLISECONDS (F62-40).  Whoever writes `timeout_s: 5` and gets `timeout: 5`
    // waits five milliseconds — the message must name the unit,
    // not just the name.
    { "timeout_s", "timeout",   " — and in MILLISECONDS (spec/62 F62-40)" },
};

// What may stand at PIPELINE level or on a STAGE.  An allow list
// and not a block list: an unknown key is an error (§20.5), and
// exactly that makes the controller's check unnecessary.
const char* kPipelineKeys[] = {
    "name", "params", "exclude", "parallel", "workspace_root", "steps", "host",
    "agent", "steplayer", "image", "base", "requires", "shares", "resources",
    "timeout", "push", "runtime", "cache", "secrets", "exec_defaults",
};
const char* kStageKeys[] = {
    "name", "steps", "run", "args", "host", "cache", "on_fail", "timeout",
    "retry", "exec_defaults", "agent", "steplayer", "image", "base",
    "requires", "shares", "resources", "resolve", "secrets", "per", "needs",
};

// F62-65: what a step may say.  Anything else is an error — a typo must not
// fall back to a default quietly.  The stage-only attributes (where a stage
// runs, what it is built from) are named as such (F62-60).
const char* kStepKeys[] = {
    "name", "per", "cache", "args", "return", "body", "needs", "host", "on_fail",
    "timeout", "retry", "pause_before", "resolve", "assert_pure", "outputs", "exec_defaults",
};
const char* kStageOnlyKeys[] = {
    "agent", "steplayer", "image", "base", "requires", "shares", "resources", "secrets", "push",
};

bool knownKey(const std::string& key, const char* const* list, size_t n) {
    for (size_t i = 0; i < n; ++i) if (key == list[i]) return true;
    return false;
}

// Text OR list → one line, comma-separated (`requires:`, F62-126).
std::string csvOf(const YAML::Node& node) {
    if (!node) return std::string();
    if (node.IsSequence()) {
        std::string out;
        for (const auto& item : node) {
            if (!out.empty()) out += ",";
            out += item.Scalar();
        }
        return out;
    }
    return node.IsScalar() ? node.Scalar() : std::string();
}

// `shares:` → "name:target:mode,…" (F62-136).  The NAME is a name, never a
// path: where the storage lives is known to the host — a pipeline that names a path
// pulls any directory of the host into its container.
std::string sharesOf(const YAML::Node& node, const std::string& where,
                     const std::string& pname, const std::string& level, bool& ok) {
    if (!node) return std::string();
    if (!node.IsSequence()) {
        LOG_ERROR(where + ": pipeline '" + pname + "': " + level + " field 'shares' is a "
                  "list of { name, to, mode } (spec/62 §18.1b)");
        ok = false;
        return std::string();
    }
    std::string out;
    for (const auto& row : node) {
        const std::string name = (row["name"] && row["name"].IsScalar()) ? row["name"].Scalar() : "";
        const std::string dest = (row["to"]   && row["to"].IsScalar())   ? row["to"].Scalar()   : "";
        std::string mode = (row["mode"] && row["mode"].IsScalar()) ? row["mode"].Scalar() : "ro";
        if (name.empty() || dest.empty()) {
            LOG_ERROR(where + ": pipeline '" + pname + "': " + level + " field 'shares' needs "
                      "'name' and 'to' per entry (spec/62 §18.1b F62-136)");
            ok = false;
            continue;
        }
        if (name.find('/') != std::string::npos) {
            LOG_ERROR(where + ": pipeline '" + pname + "': share 'name: " + name + "' is a "
                      "PATH — a share is named by its name, the host knows the location "
                      "(spec/62 F62-136)");
            ok = false;
            continue;
        }
        if (mode != "ro" && mode != "rw") {
            LOG_ERROR(where + ": pipeline '" + pname + "': share '" + name + "' has 'mode: " +
                      mode + "' — allowed are 'ro' and 'rw' (spec/62 §18.1b)");
            ok = false;
            continue;
        }
        if (!out.empty()) out += ",";
        out += name + ":" + dest + ":" + mode;
    }
    return out;
}

// `secrets:` → "name:ENV,…" (spec/62 §18.6, F62-165).  The NAME is a name,
// never a value: what it stands for is known to the host, as for `shares:`.
// ENV is the environment name the step sees; the host may derive more names
// from it (ENV_USER, ENV_PASSWORD, …), so it is a plain identifier.
bool envNameOk(const std::string& env) {
    if (env.empty()) return false;
    if (!(std::isupper((unsigned char)env[0]) || env[0] == '_')) return false;
    for (char ch : env)
        if (!(std::isupper((unsigned char)ch) || std::isdigit((unsigned char)ch) || ch == '_'))
            return false;
    return true;
}

bool envNameReserved(const std::string& env) {
    static const char* kTaken[] = {"PATH", "HOME", "TMPDIR", "LANG", "TZ", "SHELL", "USER",
                                   "PWD", "IFS", "ENV", "BASH_ENV"};
    for (const char* one : kTaken) if (env == one) return true;
    return env.rfind("COSMOS_", 0) == 0 || env.rfind("LD_", 0) == 0;
}

std::string secretsOf(const YAML::Node& node, const std::string& where,
                      const std::string& pname, const std::string& level, bool& ok) {
    if (!node) return std::string();
    if (!node.IsSequence()) {
        LOG_ERROR(where + ": pipeline '" + pname + "': " + level + " field 'secrets' is a "
                  "list of { name, env } (spec/62 §18.6)");
        ok = false;
        return std::string();
    }
    std::string out;
    for (const auto& row : node) {
        if (!row.IsMap()) {
            LOG_ERROR(where + ": pipeline '" + pname + "': " + level + " field 'secrets' is a "
                      "list of { name, env } (spec/62 §18.6)");
            ok = false;
            continue;
        }
        for (const auto& kv : row) {
            const std::string key = kv.first.Scalar();
            if (key != "name" && key != "env") {
                LOG_ERROR(where + ": pipeline '" + pname + "': secret entry has '" + key +
                          "' — an entry is { name, env } (spec/62 §18.6, F62-65)");
                ok = false;
            }
        }
        const std::string name = (row["name"] && row["name"].IsScalar()) ? row["name"].Scalar() : "";
        const std::string env  = (row["env"]  && row["env"].IsScalar())  ? row["env"].Scalar()  : "";
        if (name.empty() || env.empty()) {
            LOG_ERROR(where + ": pipeline '" + pname + "': " + level + " field 'secrets' needs "
                      "'name' and 'env' per entry (spec/62 §18.6 F62-165)");
            ok = false;
            continue;
        }
        if (name.find_first_of("/:,= \t") != std::string::npos) {
            LOG_ERROR(where + ": pipeline '" + pname + "': secret 'name: " + name + "' is not a "
                      "name — a secret is named, the host knows its value (spec/62 F62-165)");
            ok = false;
            continue;
        }
        if (!envNameOk(env)) {
            LOG_ERROR(where + ": pipeline '" + pname + "': secret '" + name + "' has 'env: " +
                      env + "' — an environment name is [A-Z_][A-Z0-9_]* (spec/62 F62-165)");
            ok = false;
            continue;
        }
        if (envNameReserved(env)) {
            LOG_ERROR(where + ": pipeline '" + pname + "': secret '" + name + "' has 'env: " +
                      env + "' — that name belongs to the executor (spec/62 F62-165)");
            ok = false;
            continue;
        }
        if (!out.empty()) out += ",";
        out += name + ":" + env;
    }
    return out;
}

// The ENV of a secret line ("name:ENV" → "ENV").
std::string secretEnv(const std::string& row) {
    size_t cut = row.find(':');
    return cut == std::string::npos ? row : row.substr(cut + 1);
}

// Secrets add up; with the same ENV the stage wins (F62-166).
std::string mergeSecrets(const std::string& at_pipe, const std::string& at_stage) {
    if (at_stage.empty()) return at_pipe;
    if (at_pipe.empty())  return at_stage;
    std::vector<std::string> mine;
    std::stringstream in(at_stage);
    std::string row;
    while (std::getline(in, row, ',')) if (!row.empty()) mine.push_back(row);
    std::string out;
    std::stringstream pin(at_pipe);
    while (std::getline(pin, row, ',')) {
        if (row.empty()) continue;
        bool taken = false;
        for (const auto& own : mine) if (secretEnv(own) == secretEnv(row)) taken = true;
        if (taken) continue;
        if (!out.empty()) out += ",";
        out += row;
    }
    for (const auto& own : mine) {
        if (!out.empty()) out += ",";
        out += own;
    }
    return out;
}

// The target of a share line ("name:target:mode" → "target").
std::string shareDest(const std::string& row) {
    size_t first = row.find(':');
    if (first == std::string::npos) return row;
    size_t second = row.find(':', first + 1);
    return row.substr(first + 1, second == std::string::npos ? std::string::npos
                                                             : second - first - 1);
}

// Shares add up; with the same TARGET the stage wins (F62-138).
std::string mergeShares(const std::string& at_pipe, const std::string& at_stage) {
    if (at_stage.empty()) return at_pipe;
    if (at_pipe.empty())  return at_stage;
    std::vector<std::string> mine;
    std::stringstream in(at_stage);
    std::string row;
    while (std::getline(in, row, ',')) if (!row.empty()) mine.push_back(row);
    std::string out;
    std::stringstream pin(at_pipe);
    while (std::getline(pin, row, ',')) {
        if (row.empty()) continue;
        bool taken = false;
        for (const auto& own : mine) if (shareDest(own) == shareDest(row)) taken = true;
        if (taken) continue;
        if (!out.empty()) out += ",";
        out += row;
    }
    for (const auto& own : mine) {
        if (!out.empty()) out += ",";
        out += own;
    }
    return out;
}

// `mem:` carries a UNIT ("512M", "8G") — without one "8" would be ambiguous, and
// an ambiguity in a limit is a trap (F62-133).
int32_t memMb(const std::string& text, const std::string& where,
              const std::string& pname, bool& ok) {
    if (text.empty()) return 0;
    char unit = text[text.size() - 1];
    int32_t num = atoi(text.c_str());
    if (unit == 'G' || unit == 'g') return num * 1024;
    if (unit == 'M' || unit == 'm') return num;
    LOG_ERROR(where + ": pipeline '" + pname + "': 'mem: " + text + "' without a unit — "
              "'512M' or '8G' (spec/62 F62-133)");
    ok = false;
    return num;
}

int32_t numAt(const YAML::Node& node, const char* key) {
    if (!node || !node[key] || !node[key].IsScalar()) return 0;
    return atoi(node[key].Scalar().c_str());
}

std::string textAt(const YAML::Node& node, const char* key) {
    if (!node || !node[key] || !node[key].IsScalar()) return std::string();
    return node[key].Scalar();
}

// spec/62 §7: the order of the reuse classes, weakest first (F62-161).
int cacheRank(const std::string& cls) {
    if (cls == "pure") return 2;
    if (cls == "resolved") return 1;
    return 0;
}

bool cacheKnown(const std::string& cls) {
    return cls == "pure" || cls == "resolved" || cls == "opaque";
}

// A value that goes as a LITERAL into generated source text is ESCAPED.
// Without that the literal ends in the middle of the value and the rest is aiml — with a
// SUBMITTED pipeline (cosmos doc/spec/96) that would be injected code.
// Escaped and not rejected, because a `run:` legitimately carries quotation marks:
// `--format="%h"` is an ordinary command line.
std::string yamlQuoted(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 2);
    for (char ch : value) {
        switch (ch) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': break;
            case '\t': out += "\\t";  break;
            default:   out += ch;
        }
    }
    return out;
}

// A line `stage_N.set_attr "key" "value"`, only if there is something.
std::string attrLine(const std::string& var, const char* key, const std::string& value) {
    if (value.empty()) return std::string();
    // An attribute is a template the host fills per lane (`agent: "${arch}"`, F62-49): it
    // is stored as written, not interpolated in the generated constructor.  aiml has no
    // escape for `${`, so the `$` comes from a variable (`__dollar`, declared there).
    std::string text = yamlQuoted(value), kept;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '$' && i + 1 < text.size() && text[i + 1] == '{') { kept += "${__dollar}"; continue; }
        kept += text[i];
    }
    return "      - call: [" + var + ".set_attr, \"" + std::string(key) + "\", \"" + kept + "\"]\n";
}

std::string limitLine(const std::string& var, const char* key, int32_t value) {
    if (value == 0) return std::string();
    return "      - call: [" + var + ".set_limit, \"" + std::string(key) + "\", " +
           std::to_string(value) + "]\n";
}

bool isIdent(const std::string& name) {
    if (name.empty()) return false;
    if (!(isalpha((unsigned char)name[0]) || name[0] == '_')) return false;
    for (char ch : name)
        if (!(isalnum((unsigned char)ch) || ch == '_')) return false;
    return true;
}

} // namespace

bool injectBuiltinModels(const std::string& text, std::string& out) {
    if (text.find("ExeOptions") == std::string::npos) return false;
    static const std::regex declared(R"((^|\n)\s*-?\s*model:\s*\n\s*name:\s*["']?ExeOptions["']?\s*(\n|$))");
    static const std::regex inlineDecl(R"(model:\s*\{[^}]*name:\s*["']?ExeOptions\b)");
    if (std::regex_search(text, declared) || std::regex_search(text, inlineDecl)) return false;
    out = text;
    if (!out.empty() && out.back() != '\n') out += "\n";
    // Every field is spec/23 §5.1's, with its default (F62-27).  `env` empty = inherit,
    // as an unset option does — a model has no "unset".
    out += "- model:\n"
           "    name: ExeOptions\n"
           "    fields:\n"
           "      - [cwd, string, \"\"]\n"
           "      - [env, map<string:string>]\n"
           "      - [shell, bool, false]\n"
           "      - [timeout, i32, -1]\n"
           "      - [capture, bool, false]\n"
           "      - [stdin, string, \"\"]\n"
           "      - [stdout, string, \"\"]\n"
           "      - [stderr, string, \"\"]\n"
           "      - [detach, bool, false]\n"
           "      - [status_file, string, \"\"]\n";
    return true;
}

bool sourceHasPipelineDecl(const std::string& text) {
    // Cheap and before parsing: only a top-level declaration
    // starts a line with "- pipeline:".
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        size_t pos = line.find_first_not_of(" \t");
        if (pos == std::string::npos) continue;
        if (line.compare(pos, 11, "- pipeline:") == 0) return true;
    }
    return false;
}

bool expandPipelineDeclarations(const std::string& text, const std::string& where,
                                std::string& out) {
    YAML::Node root;
    try {
        root = YAML::Load(text);
    } catch (const std::exception& e) {
        LOG_ERROR(where + ": pipeline rewrite: " + e.what());
        return false;
    }
    if (!root.IsSequence()) return false;

    bool ok = true;
    std::string result;

    for (const auto& decl : root) {
        if (!decl.IsMap() || !decl["pipeline"]) {
            result += "- " + indentBlock(flowOf(decl), "  ").substr(2) + "\n";
            continue;
        }
        const YAML::Node pl = decl["pipeline"];
        const std::string pname = scalarOr(pl["name"], "");
        if (pname.empty()) {
            LOG_ERROR(where + ": pipeline without a name — the name becomes a function "
                      "(spec/62 F62-108)");
            ok = false;
            continue;
        }
        // F62-157: the NAME of a pipeline is text, the FUNCTION needs an
        // identifier.  If the name already is one, the function is called exactly that
        // (F62-108, that is how one calls it locally).  Otherwise a derived
        // function name comes into being — `name: 9rooms_vom_stapel` starts with a digit
        // and so could not be compiled at all until now, although the name is perfectly
        // fine.  The value keeps carrying the REAL name (`pipe.name`),
        // and a host finds the function via `cx.names` (spec/29_2 F29_2-…i).
        std::string fname = pname;
        if (!isIdent(fname)) {
            std::string safe;
            for (char ch : fname)
                safe += (isalnum((unsigned char)ch) || ch == '_') ? ch : '_';
            if (safe.empty() || !(isalpha((unsigned char)safe[0]) || safe[0] == '_'))
                safe = "pl_" + safe;
            fname = safe;
            LOG_INFO(where + ": pipeline '" + pname + "': the name is not an identifier — the "
                     "generated function is called '" + fname + "' (spec/62 F62-157); the run "
                     "keeps carrying the name '" + pname + "'");
        }
        // Call old names by name (F110-127) — at the head as at the stage.
        for (const OldName& on : kOldNames) {
            if (pl[on.old_key]) {
                LOG_ERROR(where + ": pipeline '" + pname + "': `" + on.old_key +
                          ":` is now `" + on.now + ":`" + on.why + " — two "
                          "spellings for the same thing are never resolved later by anyone "
                          "(spec/62 §18.5)");
                ok = false;
            }
        }
        // F62-57: a pipeline has `steps:` (containing stages), a stage has
        // `steps:` (containing steps).  The rewrite read `stages:` until
        // 2026-10-25 — the spec says `steps:`, so the rewrite says it too.
        if (pl["stages"]) {
            LOG_ERROR(where + ": pipeline '" + pname + "': `stages:` is called `steps:` — a "
                      "pipeline lists its stages under `steps:`, a stage lists its "
                      "steps under it (spec/62 F62-57)");
            ok = false;
        }
        if (pl.IsMap()) {
            for (auto it = pl.begin(); it != pl.end(); ++it) {
                const std::string key = it->first.Scalar();
                if (knownKey(key, kPipelineKeys, sizeof(kPipelineKeys)/sizeof(kPipelineKeys[0])))
                    continue;
                if (key == "stages") continue;   // already reported
                bool is_old = false;
                for (const OldName& on : kOldNames) if (key == on.old_key) is_old = true;
                if (is_old) continue;            // already reported
                LOG_ERROR(where + ": pipeline '" + pname + "': unknown key `" + key +
                          ":` — what the compiler does not know is an error and not a "
                          "silent no-op (spec/62 §20.5)");
                ok = false;
            }
        }

        // The head values: default for every stage (§20.3).  They are resolved
        // HERE, once — a reader that recomputes is a second parser
        // (F62-151).
        const std::string headAgent     = textAt(pl, "agent");
        const std::string headImage     = textAt(pl, "image");
        const std::string headBase      = textAt(pl, "base");
        const std::string headLayer     = textAt(pl, "steplayer");
        const std::string headRequires  = csvOf(pl["requires"]);
        const std::string headShares    = sharesOf(pl["shares"], where, pname, "pipeline", ok);
        const std::string headSecrets   = secretsOf(pl["secrets"], where, pname, "pipeline", ok);
        const int32_t     headTimeout   = numAt(pl, "timeout");
        // F62-159: the pipeline's `cache:` is the default of every stage.
        const std::string headCache     = scalarOr(pl["cache"], "opaque");
        if (!cacheKnown(headCache)) {
            LOG_ERROR(where + ": pipeline '" + pname + "': `cache: " + headCache +
                      "` — there are `pure`, `resolved` and `opaque` (spec/62 §7)");
            ok = false;
        }
        const YAML::Node  headRes       = pl["resources"];
        const int32_t     headCpu       = numAt(headRes, "cpu");
        const int32_t     headPids      = numAt(headRes, "pids");
        const int32_t     headMem       = memMb(textAt(headRes, "mem"), where, pname, ok);

        // ── Axes ────────────────────────────────────────────────────────────
        //
        // spec/62 §4.1: an entry is `[name, type, [values]]`.  The TYPE
        // counts (F62-1): in the body `jobs` is then an i32 one can
        // compute with, and not an environment string.  The old mapping
        // `arch: [x86_64, arm64]` is rejected and names the list — two
        // spellings for the same thing are never resolved later by anyone
        // (§20.5).
        struct Axis { std::string name; std::string type; std::vector<std::string> values; };
        std::vector<Axis> axes;
        if (pl["params"] && pl["params"].IsMap()) {
            LOG_ERROR(where + ": pipeline '" + pname + "': `params:` is a LIST of "
                      "`[name, type, [values]]` (spec/62 §4.1), not a mapping "
                      "`arch: [...]` — the type of an axis counts (F62-1)");
            ok = false;
        } else if (pl["params"] && pl["params"].IsSequence()) {
            for (const auto& row : pl["params"]) {
                if (!row.IsSequence() || row.size() < 3) {
                    LOG_ERROR(where + ": pipeline '" + pname + "': a `params:` entry is "
                              "`[name, type, [values]]` (spec/62 §4.1)");
                    ok = false;
                    continue;
                }
                Axis ax;
                ax.name = row[0].Scalar();
                ax.type = row[1].Scalar();
                if (row[2].IsSequence())
                    for (const auto& v : row[2]) ax.values.push_back(v.Scalar());
                if (ax.name.empty() || !isIdent(ax.name)) {
                    LOG_ERROR(where + ": pipeline '" + pname + "': axis name '" + ax.name +
                              "' is not an identifier — it becomes a local variable in the body "
                              "(spec/62 F62-109)");
                    ok = false;
                    continue;
                }
                if (ax.type != "string" && ax.type != "i32" && ax.type != "i64" &&
                    ax.type != "f32" && ax.type != "bool") {
                    LOG_ERROR(where + ": pipeline '" + pname + "': axis '" + ax.name +
                              "': type '" + ax.type + "' cannot carry a domain of "
                              "literals — string, i32, i64, f32 or bool (spec/62 F62-1)");
                    ok = false;
                    continue;
                }
                if (ax.values.empty()) {
                    LOG_ERROR(where + ": pipeline '" + pname + "': axis '" + ax.name +
                              "' has no values — an empty domain yields no lane "
                              "(spec/62 F62-2)");
                    ok = false;
                }
                axes.push_back(ax);
            }
        }
        std::set<std::string> axisNames;
        std::map<std::string, std::string> axisTypes;
        for (const auto& ax : axes) { axisNames.insert(ax.name); axisTypes[ax.name] = ax.type; }

        std::string ctor;    // the body of the generated function <pname>()
        std::string helpers; // the generated body/probe functions
        // F62-9: step names are unique, and `needs` only names EARLIER
        // steps.  Here at compile time, so that the author finds out
        // before a run notices (the runtime checks it as well).
        std::set<std::string> declaredSteps;
        // F62-33/F62-8: what each earlier step returns and how often it runs —
        // a successor binds the value with the producer's type, and a `per: once`
        // successor of a `per: lane` step gets a list over the lanes.
        struct Produced { std::string type; std::string per; };
        std::map<std::string, Produced> producedBy;

        ctor += "      - decl: [pipe_obj, pipeline, { call: [pipeline.new] }]\n";
        ctor += "      - call: [pipe_obj.set_name, \"" + pname + "\"]\n";
        for (const auto& ax : axes) {
            const std::string var = "axis_" + ax.name;
            ctor += "      - decl: [" + var + ", list<string>]\n";
            for (const auto& v : ax.values)
                ctor += "      - call: [" + var + ".push, \"" + yamlQuoted(v) + "\"]\n";
            ctor += "      - call: [pipe_obj.add_param, \"" + ax.name + "\", \"" + ax.type +
                    "\", " + var + "]\n";
        }
        if (pl["exclude"] && pl["exclude"].IsSequence()) {
            for (const auto& ex : pl["exclude"]) {
                if (!ex.IsMap()) continue;
                std::string partial;
                for (auto it = ex.begin(); it != ex.end(); ++it) {
                    const std::string axis = it->first.Scalar();
                    if (!axisNames.count(axis)) {
                        LOG_ERROR(where + ": pipeline '" + pname + "': exclude names the axis '" +
                                  axis + "', which the pipeline does not have (spec/62 F62-4)");
                        ok = false;
                    }
                    if (!partial.empty()) partial += ",";
                    partial += axis + "=" + it->second.Scalar();
                }
                ctor += "      - call: [pipe_obj.add_exclude, \"" + yamlQuoted(partial) + "\"]\n";
            }
        }
        // `push:` — where an image goes.  `from` names a STAGE; it is checked
        // after the stages (there it is known which ones exist).
        std::vector<std::array<std::string,3>> pushes;
        if (pl["push"] && pl["push"].IsSequence()) {
            for (const auto& row : pl["push"]) {
                std::array<std::string,3> one = { textAt(row, "from"), textAt(row, "to"),
                                                  textAt(row, "tag") };
                if (one[0].empty() || one[1].empty()) {
                    LOG_ERROR(where + ": pipeline '" + pname + "': `push:` needs per entry "
                              "'from' (a stage) and 'to' (spec/62 §18, F62-71)");
                    ok = false;
                    continue;
                }
                pushes.push_back(one);
            }
        }
        if (pl["parallel"])
            ctor += "      - call: [pipe_obj.set_parallel, " + pl["parallel"].Scalar() + "]\n";
        if (pl["workspace_root"])
            ctor += "      - call: [pipe_obj.set_workspace_root, \"" +
                    pl["workspace_root"].Scalar() + "\"]\n";

        // ── Stages and steps ────────────────────────────────────────────────
        int stageNo = 0;
        std::set<std::string> declaredStages;
        if (pl["steps"] && pl["steps"].IsSequence()) {
            for (const auto& stWrap : pl["steps"]) {
                const YAML::Node stg = stWrap["stage"] ? stWrap["stage"] : stWrap;
                for (const char* bad : kUnsupportedStageFields) {
                    if (stg[bad]) {
                        LOG_ERROR(where + ": pipeline '" + pname + "': `" + bad +
                                  ":` on a stage does not say which step writes it — it belongs "
                                  "on the step (spec/62 F62-111, F62-168)");
                        ok = false;
                    }
                }
                for (const OldName& on : kOldNames) {
                    if (stg[on.old_key]) {
                        LOG_ERROR(where + ": pipeline '" + pname + "': stage field `" +
                                  on.old_key + ":` is now `" + on.now + ":" + on.why +
                                  "` (spec/62 §18.5)");
                        ok = false;
                    }
                }
                if (stg.IsMap()) {
                    for (auto it = stg.begin(); it != stg.end(); ++it) {
                        const std::string key = it->first.Scalar();
                        if (knownKey(key, kStageKeys, sizeof(kStageKeys)/sizeof(kStageKeys[0])))
                            continue;
                        bool known_bad = false;
                        for (const char* bad : kUnsupportedStageFields) if (key == bad) known_bad = true;
                        for (const OldName& on : kOldNames) if (key == on.old_key) known_bad = true;
                        if (known_bad) continue;
                        LOG_ERROR(where + ": pipeline '" + pname + "': unknown key `" +
                                  key + ":` on a stage (spec/62 §20.5)");
                        ok = false;
                    }
                }
                // spec/62 §18.2 (F62-126): what the environment must bring along.
                // Locally this is the best place for a check — the run does
                // run AGAINST this machine (F62-125).  It is checked before
                // the first step, with `sys.require_tools` (spec/23 §5.6),
                // so that there is ONE check and not two (F23-43).
                std::string stageRequires;
                if (stg["requires"]) {
                    if (stg["requires"].IsSequence()) {
                        for (const auto& r : stg["requires"]) {
                            if (!stageRequires.empty()) stageRequires += ",";
                            stageRequires += r.Scalar();
                        }
                    } else {
                        stageRequires = stg["requires"].Scalar();
                    }
                }
                const std::string sname = scalarOr(stg["name"], "stage" + std::to_string(stageNo));
                const std::string svar  = "stage_" + std::to_string(stageNo);
                declaredStages.insert(sname);
                // F62-58/F62-111: `per` on the stage is inherited by its steps
                // (innermost wins); `needs` is added to every step of it — only
                // steps of EARLIER stages, which F62-9 checks as for a step.
                const std::string stagePer = scalarOr(stg["per"], "lane");
                if (stagePer != "lane" && stagePer != "once") {
                    LOG_ERROR(where + ": pipeline '" + pname + "': stage '" + sname +
                              "': per is 'lane' or 'once', not '" + stagePer + "' (spec/62 §5.5)");
                    ok = false;
                }
                // F62-58: what the stage says for its steps, the pipeline's where
                // the stage says nothing.
                std::vector<std::pair<std::string, std::string>> stageExec;
                mergeExecDefaults(pl["exec_defaults"], stageExec, where, pname, "pipeline", ok);
                mergeExecDefaults(stg["exec_defaults"], stageExec, where, pname, "stage '" + sname + "'", ok);
                const YAML::Node stageHost = stg["host"] ? stg["host"] : pl["host"];
                std::vector<std::string> stageNeeds;
                if (stg["needs"]) {
                    if (!stg["needs"].IsSequence()) {
                        LOG_ERROR(where + ": pipeline '" + pname + "': stage '" + sname +
                                  "': `needs:` is a list of step names (spec/62 F62-111)");
                        ok = false;
                    } else {
                        for (const auto& nd : stg["needs"]) {
                            const std::string need = nd.Scalar();
                            if (!declaredSteps.count(need)) {
                                LOG_ERROR(where + ": pipeline '" + pname + "': stage '" + sname +
                                          "' needs '" + need + "' — that is no step of an earlier "
                                          "stage (spec/62 F62-9, F62-111)");
                                ok = false;
                                continue;
                            }
                            stageNeeds.push_back(need);
                        }
                    }
                }
                // A stage carries ONE command or STEPS, never both
                // (cosmos F106-11): otherwise it cannot be decided what runs first
                // and what determines the outcome.
                if (stg["run"] && stg["steps"]) {
                    LOG_ERROR(where + ": pipeline '" + pname + "': stage '" + sname +
                              "' has `run:` AND `steps:` — a stage carries a command "
                              "or steps, never both (cosmos doc/spec/106 F106-11)");
                    ok = false;
                }
                ctor += "      - decl: [" + svar + ", stage, { call: [stage.new] }]\n";
                ctor += "      - call: [" + svar + ".set_name, \"" + yamlQuoted(sname) + "\"]\n";

                // ── resolve precedence: stage beats pipeline (F62-151) ─────
                std::string stAgent = textAt(stg, "agent");
                if (stAgent.empty()) stAgent = headAgent;
                std::string stImage = textAt(stg, "image");
                if (stImage.empty()) stImage = headImage;
                std::string stBase = textAt(stg, "base");
                if (stBase.empty()) stBase = headBase;
                std::string stReq = csvOf(stg["requires"]);
                if (stReq.empty()) stReq = headRequires;
                const std::string stShares =
                    mergeShares(headShares, sharesOf(stg["shares"], where, pname, "stage", ok));
                const std::string stSecrets =
                    mergeSecrets(headSecrets, secretsOf(stg["secrets"], where, pname, "stage", ok));
                int32_t stTimeout = numAt(stg, "timeout");
                if (stTimeout == 0) stTimeout = headTimeout;

                // ── reuse (spec/62 §9.1) ─────────────────────────────────
                // F62-159: stage beats pipeline.  F62-160: `resolve:` takes the
                // form of the stage — a line beside `run:`, a body beside
                // `steps:`.  The stage's class is fixed after its steps
                // (F62-161), so `stCacheEff` starts at the declared one.
                // F62-167: a stage with secrets is opaque.  Inherited, the
                // class falls to opaque; said on the stage, it is an error —
                // the author claims a reuse that cannot be true.
                const std::string stCache =
                    scalarOr(stg["cache"], stSecrets.empty() ? headCache : std::string("opaque"));
                if (!stSecrets.empty() && stCache != "opaque") {
                    LOG_ERROR(where + ": pipeline '" + pname + "': stage '" + sname +
                              "' binds secrets and says `cache: " + stCache + "` — a stage "
                              "with secrets is opaque (spec/62 F62-167)");
                    ok = false;
                }
                if (!cacheKnown(stCache)) {
                    LOG_ERROR(where + ": pipeline '" + pname + "': stage '" + sname +
                              "': `cache: " + stCache + "` — there are `pure`, `resolved` "
                              "and `opaque` (spec/62 §7)");
                    ok = false;
                }
                const YAML::Node stResolve = stg["resolve"];
                std::string stProbe;   // the line of a run: stage
                if (stResolve && stg["run"]) {
                    if (!stResolve.IsScalar() || stResolve.Scalar().empty()) {
                        LOG_ERROR(where + ": pipeline '" + pname + "': stage '" + sname +
                                  "': beside `run:` the probe is a shell line — `resolve: \"<command>\"` "
                                  "(spec/62 F62-160/163)");
                        ok = false;
                    } else {
                        stProbe = stResolve.Scalar();
                    }
                } else if (stResolve && !stResolve.IsSequence()) {
                    LOG_ERROR(where + ": pipeline '" + pname + "': stage '" + sname +
                              "': beside `steps:` the probe is a body (a list), the default of "
                              "its `resolved` steps (spec/62 F62-160/163)");
                    ok = false;
                }
                if (stg["run"]) {
                    // F62-163 = F62-16 on the stage, with the EFFECTIVE class.
                    if (stCache == "resolved" && stProbe.empty() && !stResolve) {
                        LOG_ERROR(where + ": pipeline '" + pname + "': stage '" + sname +
                                  "' is `resolved` but has no `resolve:` — the key would hold "
                                  "nothing that moves with the name (spec/62 F62-16/163)");
                        ok = false;
                    }
                    if (stCache == "pure" && stResolve) {
                        LOG_ERROR(where + ": pipeline '" + pname + "': stage '" + sname +
                                  "' is `pure` and carries a `resolve:` — a probe belongs to "
                                  "`resolved` (spec/62 F62-16/163)");
                        ok = false;
                    }
                }
                std::string stCacheEff = stCache;
                bool sawStep = false;
                const YAML::Node stRes = stg["resources"];
                int32_t stCpu  = numAt(stRes, "cpu");
                if (stCpu == 0) stCpu = headCpu;
                int32_t stPids = numAt(stRes, "pids");
                if (stPids == 0) stPids = headPids;
                const std::string memText = textAt(stRes, "mem");
                int32_t stMem = memText.empty() ? headMem : memMb(memText, where, pname, ok);

                // `steplayer:` — WHETHER a layer comes into being per stage.  The
                // default FOLLOWS from `image:` and is not guessed
                // (F110-125); contradictory settings are rejected
                // instead of letting one of them win.
                std::string stLayer = textAt(stg, "steplayer");
                if (stLayer.empty()) stLayer = headLayer;
                if (!stLayer.empty() && stLayer != "own" && stLayer != "none") {
                    LOG_ERROR(where + ": pipeline '" + pname + "': stage '" + sname +
                              "': `steplayer: " + stLayer + "` — allowed are `own` (a layer of its "
                              "own, needs `image:`) and `none` (on the agent's "
                              "file system) (spec/62 §18.5)");
                    ok = false;
                } else if (stLayer == "none" && !stImage.empty()) {
                    LOG_ERROR(where + ": pipeline '" + pname + "': stage '" + sname +
                              "': `steplayer: none` together with `image: " + stImage +
                              "` — without layers there is no root of its own, and with an image "
                              "there are layers (spec/62 §18.5 F110-125)");
                    ok = false;
                } else if (stLayer == "own" && stImage.empty()) {
                    LOG_ERROR(where + ": pipeline '" + pname + "': stage '" + sname +
                              "': `steplayer: own` without `image:` — a layer of its own needs "
                              "a root to lie on (spec/62 §18.5 F62-147)");
                    ok = false;
                } else if (stLayer.empty()) {
                    stLayer = stImage.empty() ? "none" : "own";
                }

                ctor += attrLine(svar, "agent",     stAgent);
                ctor += attrLine(svar, "steplayer", stLayer);
                ctor += attrLine(svar, "image",     stImage);
                ctor += attrLine(svar, "base",      stBase);
                ctor += attrLine(svar, "requires",  stReq);
                ctor += attrLine(svar, "shares",    stShares);
                ctor += attrLine(svar, "secrets",   stSecrets);
                ctor += attrLine(svar, "command",   textAt(stg, "run"));
                ctor += attrLine(svar, "resolve",   stProbe);
                ctor += limitLine(svar, "cpu",     stCpu);
                ctor += limitLine(svar, "mem",     stMem);
                ctor += limitLine(svar, "pids",    stPids);
                ctor += limitLine(svar, "timeout", stTimeout);

                int stepNo = 0;
                if (stg["steps"] && stg["steps"].IsSequence()) {
                    for (const auto& spWrap : stg["steps"]) {
                        const YAML::Node sp = spWrap["step"] ? spWrap["step"] : spWrap;
                        // F62-65: every key of a step is one it may have.
                        if (sp.IsMap()) {
                            for (auto kit = sp.begin(); kit != sp.end(); ++kit) {
                                const std::string key = kit->first.Scalar();
                                if (knownKey(key, kStepKeys, sizeof(kStepKeys) / sizeof(kStepKeys[0])))
                                    continue;
                                bool oldName = false;
                                for (const OldName& on : kOldNames) if (key == on.old_key) oldName = true;
                                if (oldName) continue;                      // reported with its new name below
                                if (knownKey(key, kStageOnlyKeys, sizeof(kStageOnlyKeys) / sizeof(kStageOnlyKeys[0])))
                                    LOG_ERROR(where + ": pipeline '" + pname + "': `" + key +
                                              ":` is an attribute of the stage, not of a step — the "
                                              "steps of a stage share where it runs (spec/62 F62-60)");
                                else
                                    LOG_ERROR(where + ": pipeline '" + pname + "': unknown key `" + key +
                                              ":` on a step (spec/62 F62-65)");
                                ok = false;
                            }
                        }
                        const std::string stepName =
                            scalarOr(sp["name"], "step" + std::to_string(stepNo));
                        // F62-159: step beats stage beats pipeline.
                        const std::string cache = scalarOr(sp["cache"], stCache);
                        if (cache != "pure" && cache != "resolved" && cache != "opaque") {
                            LOG_ERROR(where + ": pipeline '" + pname + "': step '" + stepName +
                                      "': `cache: " + cache + "` — there are `pure`, `resolved` "
                                      "and `opaque` (spec/62 §7)");
                            ok = false;
                            continue;
                        }
                        if (!stSecrets.empty() && cache != "opaque") {
                            LOG_ERROR(where + ": pipeline '" + pname + "': step '" + stepName +
                                      "' says `cache: " + cache + "` in a stage that binds "
                                      "secrets — such a stage is opaque (spec/62 F62-167)");
                            ok = false;
                        }
                        // F62-161: the weakest step decides the stage.
                        if (!sawStep || cacheRank(cache) < cacheRank(stCacheEff)) stCacheEff = cache;
                        sawStep = true;
                        // F62-160: the stage's body is the probe of a
                        // `resolved` step that brings none of its own.
                        // (A default YAML::Node is a defined null and tests TRUE,
                        // so "none" is a flag here, not an empty node.)
                        const bool inheritProbe = !sp["resolve"] && cache == "resolved" &&
                                                  stResolve && stResolve.IsSequence();
                        const bool hasProbe = sp["resolve"] || inheritProbe;
                        const YAML::Node spResolve = inheritProbe ? stResolve : sp["resolve"];
                        const std::string perMode = scalarOr(sp["per"], stagePer);
                        if (perMode != "lane" && perMode != "once") {
                            LOG_ERROR(where + ": pipeline '" + pname + "': step '" + stepName +
                                      "': per is 'lane' or 'once', not '" + perMode +
                                      "' (spec/62 §5.5)");
                            ok = false;
                        }
                        // F62-115: a one-off step cannot depend on an
                        // axis — there is ONE result for the whole
                        // matrix.  The alternative ("take the first lane") would be
                        // a result that depends on the lane order.
                        if (perMode == "once" && sp["args"] && sp["args"].IsSequence() &&
                            sp["args"].size() > 0) {
                            LOG_ERROR(where + ": pipeline '" + pname + "': step '" + stepName +
                                      "' is 'per: once' and still declares axes — a "
                                      "one-off step cannot depend on an axis "
                                      "(spec/62 F62-115)");
                            ok = false;
                        }
                        if (!declaredSteps.insert(stepName).second) {
                            LOG_ERROR(where + ": pipeline '" + pname + "': the step name '" +
                                      stepName + "' occurs twice — then it cannot be "
                                      "decided which step a 'needs' means "
                                      "(spec/62 F62-9)");
                            ok = false;
                        }
                        std::vector<std::string> needNames = stageNeeds;   // F62-111
                        if (sp["needs"] && sp["needs"].IsSequence()) {
                            for (const auto& nd : sp["needs"]) {
                                const std::string need = nd.Scalar();
                                if (!declaredSteps.count(need)) {
                                    LOG_ERROR(where + ": pipeline '" + pname + "': step '" +
                                              stepName + "' needs '" + need + "' — that is not an "
                                              "earlier declared step (spec/62 F62-9)");
                                    ok = false;
                                    continue;
                                }
                                if (std::find(needNames.begin(), needNames.end(), need) == needNames.end())
                                    needNames.push_back(need);
                            }
                        }
                        const std::string fnBase = "__pl_" + pname + "_" +
                                                   std::to_string(stageNo) + "_" +
                                                   std::to_string(stepNo);

                        // Declared axes + host names become local
                        // variables in the body (F62-109).
                        std::string prelude;
                        std::vector<std::string> declaredArgs;
                        // F62-59: a step without `args:` has the stage's whole set — a
                        // `per: once` step has no lane and so no axes (F62-115).
                        const YAML::Node argsNode =
                            sp["args"] ? sp["args"] : (perMode == "lane" ? stg["args"] : YAML::Node());
                        if (argsNode && argsNode.IsSequence()) {
                            for (const auto& a : argsNode) {
                                // `[name, type]` as on a stage (§5.0) or just the name.
                                const std::string axis =
                                    a.IsSequence() && a.size() > 0 ? a[0].Scalar() : a.Scalar();
                                if (!axisNames.count(axis)) {
                                    LOG_ERROR(where + ": pipeline '" + pname + "': step '" +
                                              stepName + "' uses the axis '" + axis +
                                              "', which the pipeline does not have (spec/62 F62-1)");
                                    ok = false;
                                }
                                declaredArgs.push_back(axis);
                                // The axis appears in the body with its
                                // DECLARED type (F62-1): `jobs` is an
                                // i32 one can compute with.  The channel
                                // carries text, so it is converted here — once,
                                // in one place.
                                const std::string axType = axisTypes.count(axis)
                                                               ? axisTypes[axis] : "string";
                                if (axType == "string") {
                                    prelude += "      - decl: [" + axis +
                                               ", string, { call: [step.arg, \"" + axis + "\"] }]\n";
                                } else {
                                    const std::string raw = "__ax_" + axis;
                                    prelude += "      - decl: [" + raw +
                                               ", string, { call: [step.arg, \"" + axis + "\"] }]\n";
                                    if (axType == "bool")
                                        prelude += "      - decl: [" + axis + ", bool, { eq: [" +
                                                   raw + ", \"true\"] }]\n";
                                    else if (axType == "f32")
                                        prelude += "      - decl: [" + axis + ", f32, { call: [" +
                                                   raw + ".parse_f32] }]\n";
                                    else
                                        prelude += "      - decl: [" + axis + ", " + axType +
                                                   ", { call: [" + raw + ".parse_i32] }]\n";
                                }
                            }
                        }
                        std::vector<std::string> hostNames;
                        const YAML::Node hostNode = sp["host"] ? sp["host"] : stageHost;   // F62-58
                        if (hostNode && hostNode.IsSequence()) {
                            for (const auto& h : hostNode) {
                                const std::string hname = h.Scalar();
                                // F62-17: host carries NAMES, never name=value — a
                                // value in the source text would be a secret in the source text.
                                if (hname.find('=') != std::string::npos) {
                                    LOG_ERROR(where + ": pipeline '" + pname + "': step '" +
                                              stepName + "': host names '" + hname +
                                              "' — host carries names, never name=value "
                                              "(spec/62 F62-17)");
                                    ok = false;
                                    continue;
                                }
                                hostNames.push_back(hname);
                                prelude += "      - decl: [" + hname +
                                           ", string, { call: [step.host, \"" + hname + "\"] }]\n";
                            }
                        }
                        // F62-33/F62-121: the results of the `needs` appear as
                        // local names equal to the step name — that is why one writes
                        // `${clone}` and means the value that `clone` has published.
                        // A name that is already an axis would be declared twice;
                        // that is an error and not a precedence rule.
                        for (const auto& need : needNames) {
                            if (std::find(declaredArgs.begin(), declaredArgs.end(), need) !=
                                declaredArgs.end()) {
                                LOG_ERROR(where + ": pipeline '" + pname + "': step '" +
                                          stepName + "': '" + need + "' is an axis AND in needs — "
                                          "then the name would be bound twice in the body");
                                ok = false;
                                continue;
                            }
                            const Produced prod = producedBy.count(need) ? producedBy[need]
                                                                         : Produced{"string", "lane"};
                            const bool fanIn = (perMode == "once" && prod.per == "lane");
                            prelude += needBinding(need, prod.type, fanIn);
                        }
                        prelude += "      - decl: [workspace, string, { call: [step.workspace] }]\n";
                        // §5.2: host_env — exactly the names declared in `host:`,
                        // nothing else, as the environment a pure step passes (F62-31).
                        prelude += "      - decl: [host_env, map<string:string>]\n";
                        for (const auto& hn : hostNames)
                            prelude += "      - call: [host_env.put, \"" + hn + "\", " + hn + "]\n";
                        // F62-61: the effective exec_defaults, as a value in the body.
                        std::vector<std::pair<std::string, std::string>> stepExec = stageExec;
                        mergeExecDefaults(sp["exec_defaults"], stepExec, where, pname,
                                          "step '" + stepName + "'", ok);
                        bool execEnv = false;
                        if (!stepExec.empty()) {
                            prelude += "      - decl: [__exec_defaults, ExeOptions]\n";
                            for (const auto& [field, val] : stepExec) {
                                prelude += "      - set: [__exec_defaults." + field + ", " + val + "]\n";
                                if (field == "env") execEnv = true;
                            }
                        }
                        // F62-31: a pure step passes env; where the call shows its options,
                        // that is checked here, otherwise it is said.
                        if (cache == "pure" && sp["body"]) {
                            const int verdict = exeEnvVerdict(sp["body"], execEnv);
                            if (verdict == 1) {
                                LOG_ERROR(where + ": pipeline '" + pname + "': step '" + stepName +
                                          "' is pure, and a sys.exe in it inherits the environment — "
                                          "pass `env: host_env`, or set it in exec_defaults (spec/62 F62-31)");
                                ok = false;
                            } else if (verdict == 2) {
                                LOG_WARNING(where + ": pipeline '" + pname + "': step '" + stepName +
                                            "' is pure, and a sys.exe in it takes its options from a "
                                            "value — whether it sets env is not visible here (spec/62 F62-31)");
                            }
                        }

                        const std::string retType = scalarOr(sp["return"], "i32");
                        producedBy[stepName] = Produced{retType, perMode};
                        helpers += "- def:\n";
                        helpers += "    name: " + fnBase + "_body\n";
                        helpers += "    return: " + retType + "\n";
                        helpers += "    body:\n";
                        helpers += prelude;
                        // The stage's preconditions apply before its first
                        // step; after that half of the work would already be
                        // done, and the message would come too late (F62-126).
                        if (stepNo == 0 && !stageRequires.empty()) {
                            helpers += "      - decl: [__missing_env, string, { call: [sys.require_tools, \"" +
                                       stageRequires + "\"] }]\n";
                            helpers += "      - if:\n";
                            helpers += "          condition: { gt: [__missing_env.len, 0] }\n";
                            helpers += "          then:\n";
                            helpers += "            - call: [log.error, \"stage '" + sname +
                                       "': environment incomplete —${__missing_env} (requires:, spec/62 §18.2)\"]\n";
                            helpers += "            - throw: \"requires: environment incomplete —${__missing_env}\"\n";
                        }
                        if (sp["body"] && sp["body"].IsSequence()) {
                            YAML::Node body = YAML::Clone(sp["body"]);
                            if (!stepExec.empty()) seedExeOptionDecls(body);
                            helpers += bodyBlock(body, "      ");
                        }
                        else
                            helpers += "      - return: 0\n";
                        helpers += "\n";

                        // F62-121: `return: <type>` becomes a wrapper that calls the
                        // written body and puts its result into the channel
                        // (§13.5h).  The author keeps writing `- return: rep`
                        // and needs to know nothing about set_result.  Only for types
                        // the channel carries: string or number (F62-118).
                        // F62-33/34: any other type goes through the channel in its
                        // serialised form (spec/29) — the author writes `return: rep`
                        // for a model as for a string, and the successor gets the model.
                        const bool asText =
                            (retType == "string" || retType == "i32" || retType == "i64" ||
                             retType == "u32" || retType == "u64");
                        std::string bundledFn = fnBase + "_body";
                        if (retType != "void") {
                            helpers += "- def:\n";
                            helpers += "    name: " + fnBase + "_pub\n";
                            helpers += "    return: i32\n";
                            helpers += "    body:\n";
                            helpers += "      - decl: [pub_val, " + retType + ", { call: [" +
                                       fnBase + "_body] }]\n";
                            if (asText) {
                                helpers += "      - call: [step.set_result, pub_val]\n";
                            } else {
                                helpers += "      - decl: [pub_txt, string, { call: [sys.serialize, pub_val, \"" +
                                           retType + "\"] }]\n";
                                helpers += "      - call: [step.set_result, pub_txt]\n";
                            }
                            helpers += "      - return: 0\n";
                            helpers += "\n";
                            bundledFn = fnBase + "_pub";
                        }
                        bool wantPure   = (cache == "pure");
                        bool assertPure = sp["assert_pure"] && sp["assert_pure"].Scalar() == "true";
                        std::string bundleOpts = "{ context: true";
                        if (wantPure)   bundleOpts += ", pure: true";
                        if (assertPure) bundleOpts += ", assert_pure: true";
                        bundleOpts += " }";

                        if (hasProbe && spResolve.IsSequence()) {
                            helpers += "- def:\n";
                            helpers += "    name: " + fnBase + "_resolve\n";
                            helpers += "    return: { type: string, lifetime: producer }\n";
                            helpers += "    body:\n";
                            helpers += prelude;
                            helpers += bodyBlock(spResolve, "      ");
                            helpers += "\n";
                        }

                        const std::string pvar = "step_" + std::to_string(stageNo) + "_" +
                                                 std::to_string(stepNo);
                        ctor += "      - decl: [" + pvar + ", step, { call: [step.new] }]\n";
                        ctor += "      - call: [" + pvar + ".set_name, \"" + stepName + "\"]\n";
                        ctor += "      - call: [" + pvar + ".set_body, { call: [sys.bundle, \"" +
                                bundledFn + "\", " + bundleOpts + "] }]\n";
                        ctor += "      - call: [" + pvar + ".set_cache, CacheClass." + cache + "]\n";
                        for (const auto& axis : declaredArgs)
                            ctor += "      - call: [" + pvar + ".use_arg, \"" + axis + "\"]\n";
                        for (const auto& hn : hostNames)
                            ctor += "      - call: [" + pvar + ".use_host, \"" + hn + "\"]\n";
                        if (hasProbe)
                            ctor += "      - call: [" + pvar + ".set_resolve, " + fnBase +
                                    "_resolve]\n";
                        const YAML::Node onFail = sp["on_fail"] ? sp["on_fail"] : stg["on_fail"];   // F62-58
                        if (onFail) {
                            std::string reach = onFail.Scalar();
                            // F62-94: `continue` is a keyword in aiml, the
                            // enum value is called `proceed`.  The same concept, two
                            // spellings — this is where the rewrite happens.
                            if (reach == "continue") reach = "proceed";
                            ctor += "      - call: [" + pvar + ".set_on_fail, OnFail." + reach + "]\n";
                        }
                        // F62-58: a step without its own `timeout:` has the stage's, the
                        // stage the pipeline's (the stage's also stays its limit, §18.3).
                        const YAML::Node tmoNode =
                            sp["timeout"] ? sp["timeout"] : (stg["timeout"] ? stg["timeout"] : pl["timeout"]);
                        if (tmoNode && tmoNode.IsScalar())
                            ctor += "      - call: [" + pvar + ".set_timeout, " +
                                    tmoNode.Scalar() + "]\n";
                        const YAML::Node retryNode = sp["retry"] ? sp["retry"] : stg["retry"];   // F62-58
                        if (retryNode)
                            ctor += "      - call: [" + pvar + ".set_retry, " +
                                    retryNode.Scalar() + "]\n";
                        if (sp["pause_before"] && sp["pause_before"].Scalar() == "true")
                            ctor += "      - call: [" + pvar + ".set_pause_before, 1]\n";
                        if (perMode == "once")
                            ctor += "      - call: [" + pvar + ".set_per, Per.once]\n";
                        for (const auto& need : needNames)
                            ctor += "      - call: [" + pvar + ".add_need, \"" + need + "\"]\n";
                        // F62-168: the output tree.
                        if (sp["outputs"]) {
                            if (!sp["outputs"].IsSequence()) {
                                LOG_ERROR(where + ": pipeline '" + pname + "': step '" + stepName +
                                          "': `outputs:` is a list of paths and globs (spec/62 F62-168)");
                                ok = false;
                            } else {
                                for (const auto& o : sp["outputs"]) {
                                    const std::string pat = o.Scalar();
                                    if (pat.empty() || pat[0] == '/' || pat.find("..") != std::string::npos) {
                                        LOG_ERROR(where + ": pipeline '" + pname + "': step '" + stepName +
                                                  "': the output '" + pat + "' must lie in the workspace — "
                                                  "relative, without '..' (spec/62 F62-168)");
                                        ok = false;
                                        continue;
                                    }
                                    ctor += "      - call: [" + pvar + ".add_output, \"" + yamlQuoted(pat) + "\"]\n";
                                }
                            }
                        }
                        ctor += "      - call: [" + svar + ".add_step, " + pvar + "]\n";
                        ++stepNo;
                    }
                }
                // F62-164: the reader returns the resolved class.
                ctor += attrLine(svar, "cache", stCacheEff);
                ctor += "      - call: [pipe_obj.add_stage, " + svar + "]\n";
                ++stageNo;
            }
        }
        // `push:` only now: `from` names a STAGE, and which ones exist
        // is only fixed after the loop (F62-71).
        for (const auto& one : pushes) {
            if (!declaredStages.count(one[0])) {
                LOG_ERROR(where + ": pipeline '" + pname + "': `push: { from: " + one[0] +
                          " }` names no stage of this pipeline — an image is a "
                          "file system, and that belongs to a stage (spec/62 F62-71)");
                ok = false;
                continue;
            }
            ctor += "      - call: [pipe_obj.add_push, \"" + yamlQuoted(one[0]) + "\", \"" +
                    yamlQuoted(one[1]) + "\", \"" + yamlQuoted(one[2]) + "\"]\n";
        }
        ctor += "      - return: pipe_obj\n";

        result += helpers;
        result += "- def:\n";
        result += "    name: " + fname + "\n";
        result += "    return: pipeline\n";
        result += "    body:\n";
        if (ctor.find("${__dollar}") != std::string::npos)
            result += "      - decl: [__dollar, string, \"$\"]\n";
        result += ctor;
        result += "\n";
    }

    if (!ok) return false;
    out = result;
    return true;
}

} // namespace aiml
