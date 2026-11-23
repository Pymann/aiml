// spec/62 §13.5 — composed pipelines.
//
// The structure comes into being at runtime, the BODY stays a code bundle: that way
// the purity check (on the body, at compile time) and the
// content-derived key still apply (F62-85).
//
// Two things are deliberately strict here:
//   * The structure is checked completely BEFORE the first step (F62-84).
//     A half-run run from a broken structure would leave behind
//     cache entries for a pipeline that never existed in that form.
//   * The key contains only the axes a step DECLARES
//     (use_arg, F62-86) — otherwise one loses reuse across the
//     axes that are not used.
#include "pipeline_runtime.h"
#include "string_runtime.h"
#include "../../logger.h"
#include "../../exec_impl.h"
#include "../core/path_runtime.h"

#include <algorithm>
#include <atomic>
#include <unistd.h>
#include <chrono>
#include <mutex>
#include <thread>
#include <map>
#include <set>
#include <string>
#include <vector>

// Content address of a code bundle (FNV-1a of the chunks), defined in
// src/core/code_runtime.cpp — here the part of the step key that captures the
// BODY (spec/62 F62-85).
extern "C" const char* aiml_code_id(void* code);
// spec/62 F62-103: was the body bundled with `pure: true` (i.e. checked)?
extern "C" int32_t aiml_code_is_pure(void* code);

namespace {

struct AimlStringList { int32_t size; int32_t capacity; char** data; };

// spec/62 F62-90: what the currently running step says about itself.
// Thread-local, because lanes may run in threads (§6) — a shared
// slot would let two lanes overwrite each other's results.
struct CurStep {
    bool        active        = false;
    // §13.5e: what this step sees in THIS lane.  `declared` is the
    // set from use_arg — reading an undeclared axis is an
    // error and not an empty value (F62-106), otherwise a blind
    // key would arise.
    std::map<std::string, std::string> bindings;
    std::set<std::string>              declared;
    std::map<std::string, std::string> host;
    std::string                        workspace;
    // §13.5h: what this body publishes, and what it MAY read.
    // `need_results` contains only the results of the DECLARED needs —
    // anything else would be a blind key (F62-119).
    std::string                        result;
    bool                               result_set = false;
    std::map<std::string, std::string> need_results;
    bool        status_set    = false;
    int32_t     status        = -1;
    int         fail_reach    = -1;   // -1 none, 0 lane, 1 pipeline
    std::string fail_msg;
};
thread_local CurStep g_cur;

// spec/62 F62-93: the precedence order of F62-43 — PROVISIONAL until §16
// item 6 fixes the meanings.  It is in ONE place here, so that
// exactly one line has to change when it is decided.  All that is decided
// is: the order is total, and aggregation happens by choosing the
// highest rank, not by additive mixing.
// StepStatus: 0 red, 1 green, 2 blue, 3 yellow, 4 cyan, 5 magenta.
const int kStatusPrecedence[6] = { 5, 0, 1, 3, 2, 4 };

int32_t status_max(int32_t a, int32_t b) {
    if (a < 0) return b;
    if (b < 0) return a;
    if (a > 5 || b > 5) return (a > 5) ? b : a;
    return (kStatusPrecedence[a] >= kStatusPrecedence[b]) ? a : b;
}

struct PStep {
    std::string           name;
    void*                 body = nullptr;   // Code*
    int32_t               cache = 0;        // CacheClass: 0=opaque 1=resolved 2=pure
    std::vector<std::string> args;          // deklarierte Achsen (F62-86)
    void*                 resolve = nullptr;  // probe (§9); only with cache==resolved
    int32_t               on_fail = 0;        // 0=lane 1=pipeline 2=continue (F62-39)
    int32_t               timeout_ms = 0;     // 0 = no limit (F62-40)
    int32_t               retry = 1;          // Versuche (F62-41)
    int32_t               status = -1;        // StepStatus, gueltig ab done (F62-44)
    int32_t               pause_before = 0;   // deklariertes Tor (F62-54)
    int32_t               per = 0;            // 0 = lane, 1 = once (§5.5)
    std::vector<std::string> needs;            // only earlier steps (F62-9)
};

struct PStage {
    std::string        name;
    std::string        runs_on;      // `agent:` — WHO takes the step (F110-127)
    // spec/62 §20.3: these are the RESOLVED values — the rewrite has already
    // applied the precedence (stage beats pipeline, F62-151).  A
    // reader recomputes nothing, otherwise it is a second parser (§20.1).
    std::string        steplayer;    // `own` | `none` (§18.5, F110-124/125)
    std::string        image;        // WORIN (F62-66)
    std::string        base;         // WORAUF: run:<lauf> | job:<kennung> (F62-144)
    std::string        requires_on;  // "git,gcc>=13" (§18.2, F62-126)
    std::string        shares;       // "name:/ziel:modus,…" (§18.1b, F62-136)
    std::string        command;      // `run:` — the command of slice 1
    int32_t            cpu_cores = 0;   // 0 = host default, NOT unlimited
    int32_t            mem_mb    = 0;
    int32_t            pids_max  = 0;
    int32_t            timeout_ms = 0;  // 0 = Vorgabe des Ausfuehrenden (F62-40)
    std::vector<PStep> steps;
};

// `push:` — where an image goes (§18, F62-69/71).  `from` names the STAGE.
struct PPush {
    std::string from;
    std::string to;
    std::string tag;
};

struct PPipeline {
    std::string                                       name;
    std::vector<std::map<std::string, std::string>>   excludes;   // Teilbindungen (§4.2)
    std::vector<std::pair<std::string, std::vector<std::string>>> params;  // axis -> values
    std::vector<PStage>                               stages;
    std::vector<PPush>                                pushes;
    // spec/62 §4.1 (F62-1): the TYPE of an axis.  It counts in the body (`jobs`
    // is an i32, not an environment string) and therefore belongs to the shape of the
    // run that a host reads (§20.4).
    std::map<std::string, std::string>                param_types;
    // Ergebniszaehler des letzten Laufs
    int32_t reused   = 0;
    int32_t ran      = 0;
    int32_t attempts = 0;   // including the failed ones (§5.6)
    int32_t status   = -1;  // aggregated over the steps (F62-43)
    // §5.9: Lebenszyklus getrennt vom Ausgang (F62-44 ergaenzt).
    int32_t lifecycle    = 0;   // 0 pending 1 running 2 paused 3 done
    bool    pause_wanted = false;
    std::set<std::string> released;   // freigegebene Tore (F62-54)
    std::string workspace_root = ".aiml_pipeline";
    int32_t parallel = 1;   // gleichzeitige Lanes (§6); 0 = auto
    // Step cache: key -> (exit code, result).  The result MUST
    // be in it (F62-120/F62-37): otherwise a reused
    // step would be invisible to its own successors.  Deliberately plain;
    // the real layer cache is with the host (cosmos spec/96).
    struct CacheEntry { int32_t rc = 0; std::string result; };
    std::map<std::string, CacheEntry> cache;
};

// Lane = a complete binding of all axes.
using Bindings = std::map<std::string, std::string>;

void expand(const std::vector<std::pair<std::string, std::vector<std::string>>>& params,
            size_t idx, Bindings current, std::vector<Bindings>& out) {
    if (idx >= params.size()) { out.push_back(current); return; }
    for (const auto& v : params[idx].second) {
        current[params[idx].first] = v;
        expand(params, idx + 1, current, out);
    }
}

// Bindings as readable text, stably sorted (the map is ordered).  ONE
// format for lane text, observer `bindings`, workspace name and the
// controller's record (spec/62 F62-152) — two formats for the same
// concept mean that display text is being parsed somewhere.
std::string bind_text(const Bindings& b) {
    std::string out;
    for (const auto& kv : b) {
        if (!out.empty()) out += ",";
        out += kv.first + "=" + kv.second;
    }
    return out;
}

// Key of a (step, lane): bundle address + DECLARED bindings,
// length-prefixed as in spec/62 §8 — a separator can occur in the value.
std::string step_key(const PStep& st, const Bindings& bind, const std::string& resolved) {
    std::string mat;
    const char* bundle_id = aiml_code_id(st.body);
    std::string bid = bundle_id ? bundle_id : "";
    mat += std::to_string(bid.size()) + ":" + bid;
    mat += std::to_string(st.cache) + ":c";
    for (const auto& axis : st.args) {               // already sorted (std::set-like)
        auto it = bind.find(axis);
        std::string pair = axis + "=" + (it == bind.end() ? "" : it->second);
        mat += std::to_string(pair.size()) + ":" + pair;
    }
    // Probe result: empty for pure/opaque, otherwise the resolved address (§9).
    mat += std::to_string(resolved.size()) + ":" + resolved;
    return mat;
}

} // namespace

extern "C" {

void* aiml_step_new(void) { return new PStep(); }
void  aiml_step_set_name(void* s, const char* n) { if (s && n) static_cast<PStep*>(s)->name = n; }
void  aiml_step_set_body(void* s, void* code)    { if (s) static_cast<PStep*>(s)->body = code; }
void  aiml_step_set_cache(void* s, int32_t c)    { if (s) static_cast<PStep*>(s)->cache = c; }
void  aiml_step_use_arg(void* s, const char* a)  {
    if (!s || !a) return;
    auto* st = static_cast<PStep*>(s);
    // insert sorted: key formation must not depend on the
    // order of calls (spec/62 §8).
    std::string axis(a);
    for (const auto& have : st->args) if (have == axis) return;
    st->args.push_back(axis);
    std::sort(st->args.begin(), st->args.end());
}

void aiml_step_set_resolve(void* s, void* fn) { if (s) static_cast<PStep*>(s)->resolve = fn; }

void* aiml_stage_new(void) { return new PStage(); }
void  aiml_stage_set_name(void* g, const char* n)     { if (g && n) static_cast<PStage*>(g)->name = n; }
void  aiml_stage_set_runs_on(void* g, const char* l)  { if (g && l) static_cast<PStage*>(g)->runs_on = l; }
void  aiml_stage_add_step(void* g, void* s) {
    if (!g || !s) return;
    static_cast<PStage*>(g)->steps.push_back(*static_cast<PStep*>(s));
}

void* aiml_pipeline_new(void) { return new PPipeline(); }
void  aiml_pipeline_set_name(void* p, const char* n) { if (p && n) static_cast<PPipeline*>(p)->name = n; }

void aiml_pipeline_add_param(void* p, const char* axis, const char* type, const void* values) {
    if (!p || !axis || !values) return;
    if (type && *type) static_cast<PPipeline*>(p)->param_types[axis] = type;
    const auto* lst = static_cast<const AimlStringList*>(values);
    std::vector<std::string> vals;
    for (int32_t i = 0; i < lst->size; ++i)
        if (lst->data[i]) vals.push_back(lst->data[i]);
    static_cast<PPipeline*>(p)->params.push_back({std::string(axis), vals});
}

// "arch=arm64,compiler=gcc" -> { arch: arm64, compiler: gcc }
static std::map<std::string, std::string> parse_partial(const std::string& txt) {
    std::map<std::string, std::string> out;
    size_t pos = 0;
    while (pos <= txt.size()) {
        size_t comma = txt.find(',', pos);
        std::string item = txt.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        size_t eq = item.find('=');
        if (eq != std::string::npos) out[item.substr(0, eq)] = item.substr(eq + 1);
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    return out;
}

void aiml_step_set_on_fail(void* st, int32_t reach) {
    if (st && reach >= 0 && reach <= 2) static_cast<PStep*>(st)->on_fail = reach;
}
void aiml_step_set_timeout(void* st, int32_t ms) {
    if (st && ms >= 0) static_cast<PStep*>(st)->timeout_ms = ms;
}
void aiml_step_set_retry(void* st, int32_t tries) {
    if (st && tries >= 1) static_cast<PStep*>(st)->retry = tries;
}
int32_t aiml_step_status(void* st) { return st ? static_cast<PStep*>(st)->status : -1; }
void aiml_step_set_pause_before(void* st, int32_t on) {
    if (st) static_cast<PStep*>(st)->pause_before = (on != 0) ? 1 : 0;
}
void aiml_step_set_per(void* st, int32_t per) {
    if (st && per >= 0 && per <= 1) static_cast<PStep*>(st)->per = per;
}
void aiml_step_add_need(void* st, const char* name) {
    if (st && name && *name) static_cast<PStep*>(st)->needs.push_back(name);
}

// F62-90: without a running step this is an ERROR, not a silent no-op — otherwise
// a `fail_lane` vanishes into nothing that is called precisely
// when something goes wrong.
void aiml_step_status_set(int32_t status) {
    if (!g_cur.active) { LOG_ERROR("set_status: no running step"); return; }
    if (status < 0 || status > 5) { LOG_ERROR("set_status: not a StepStatus value"); return; }
    g_cur.status_set = true;
    g_cur.status     = status;
}
void aiml_step_fail_lane(const char* msg) {
    if (!g_cur.active) { LOG_ERROR("fail_lane: no running step"); return; }
    g_cur.fail_reach = 0;
    g_cur.fail_msg   = msg ? msg : "";
}
void aiml_step_fail_pipeline(const char* msg) {
    if (!g_cur.active) { LOG_ERROR("fail_pipeline: no running step"); return; }
    g_cur.fail_reach = 1;
    g_cur.fail_msg   = msg ? msg : "";
}

void aiml_pipeline_add_exclude(void* p, const char* partial) {
    if (!p || !partial) return;
    static_cast<PPipeline*>(p)->excludes.push_back(parse_partial(partial));
}

// matches: does the partial binding apply to this lane?  Partial binding means:
// only the named axes count, the rest does not matter (§4.2) — otherwise one would have to
// repeat every statement for every value of the remaining axes.
static bool excluded(const std::vector<std::map<std::string, std::string>>& excludes,
                     const Bindings& bind) {
    for (const auto& ex : excludes) {
        bool all = true;
        for (const auto& kv : ex) {
            auto it = bind.find(kv.first);
            if (it == bind.end() || it->second != kv.second) { all = false; break; }
        }
        if (all) return true;
    }
    return false;
}

void aiml_pipeline_add_stage(void* p, void* g) {
    if (!p || !g) return;
    static_cast<PPipeline*>(p)->stages.push_back(*static_cast<PStage*>(g));
}

int32_t aiml_pipeline_lane_count(void* p) {
    if (!p) return 0;
    auto* pp = static_cast<PPipeline*>(p);
    std::vector<Bindings> lanes;
    expand(pp->params, 0, Bindings{}, lanes);
    int32_t n = 0;
    for (const auto& b : lanes) if (!excluded(pp->excludes, b)) ++n;
    return n;
}

int32_t aiml_pipeline_reused_count(void* p) { return p ? static_cast<PPipeline*>(p)->reused : 0; }
int32_t aiml_pipeline_ran_count(void* p)    { return p ? static_cast<PPipeline*>(p)->ran    : 0; }
int32_t aiml_pipeline_attempt_count(void* p) { return p ? static_cast<PPipeline*>(p)->attempts : 0; }
int32_t aiml_pipeline_status(void* p)        { return p ? static_cast<PPipeline*>(p)->status   : -1; }

// §5.9: pause from outside.  Takes effect AFTER the running step (F62-55) — a
// body has live child processes, and stopping it in the middle of the run would leave
// a half-written workspace behind, which would then be hashed and
// delivered later as a hit.
void aiml_pipeline_request_pause(void* p) {
    if (p) static_cast<PPipeline*>(p)->pause_wanted = true;
}
void aiml_pipeline_release(void* p, const char* name) {
    if (p && name) static_cast<PPipeline*>(p)->released.insert(name);
}
// Resuming needs no state of its own (F62-56): only clear the
// flag.  What is resumed is in the cache.
void aiml_pipeline_resume(void* p) {
    if (!p) return;
    auto* pp = static_cast<PPipeline*>(p);
    pp->pause_wanted = false;
    pp->lifecycle    = 0;
}
int32_t aiml_pipeline_lifecycle(void* p) { return p ? static_cast<PPipeline*>(p)->lifecycle : 0; }

// §6 / F62-98: limits concurrent LANES, not processes.  Not
// key material — two runs with 1 and with 8 must yield the same
// keys.
void aiml_pipeline_set_parallel(void* p, int32_t n) {
    if (p && n >= 0) static_cast<PPipeline*>(p)->parallel = n;
}

void aiml_pipeline_set_workspace_root(void* p, const char* root) {
    if (p && root && *root) static_cast<PPipeline*>(p)->workspace_root = root;
}

// §13.5e: the body reads its lane.  Without a running step this is an error —
// the same principle as for set_status (F62-90): no silent no-op.
char* aiml_step_arg(const char* axis) {
    if (!g_cur.active) { LOG_ERROR("step.arg: no running step"); return aiml_str_alloc(0); }
    std::string name = axis ? axis : "";
    // F62-106: undeclared means error, not empty.  An empty value
    // would hide exactly the hole the key is supposed to close.
    if (!g_cur.declared.count(name)) {
        LOG_ERROR("step.arg('" + name + "'): this axis is not declared with use_arg — "
                  "it would act on the result without being in the key "
                  "(spec/62 F62-106)");
        g_cur.fail_reach = 0;
        g_cur.fail_msg   = "step.arg('" + name + "') not declared";
        return aiml_str_alloc(0);
    }
    auto it = g_cur.bindings.find(name);
    const std::string val = (it != g_cur.bindings.end()) ? it->second : std::string();
    char* out = aiml_str_alloc(val.size());
    if (!val.empty()) memcpy(out, val.data(), val.size());
    return out;
}

// §13.5h: publish the result of this step.  A number is written in
// decimal — what is recorded is the serialised form (F62-118).
void aiml_step_set_result(const char* value) {
    if (!g_cur.active) { LOG_ERROR("step.set_result: no running step"); return; }
    g_cur.result     = value ? value : "";
    g_cur.result_set = true;
}

// F62-119: only a DECLARED need may be read.  A foreign
// result would otherwise act on the body without being in the key via input_keys
// — exactly the blind key that §1.1 describes.
char* aiml_step_need_result(const char* name) {
    if (!g_cur.active) { LOG_ERROR("step.need_result: no running step"); return aiml_str_alloc(0); }
    std::string key = name ? name : "";
    auto it = g_cur.need_results.find(key);
    if (it == g_cur.need_results.end()) {
        LOG_ERROR("step.need_result('" + key + "'): this step is not in the needs — "
                  "its result would take effect without being in the key "
                  "(spec/62 F62-119)");
        g_cur.fail_reach = 0;
        g_cur.fail_msg   = "step.need_result('" + key + "') not in needs";
        return aiml_str_alloc(0);
    }
    char* out = aiml_str_alloc(it->second.size());
    if (!it->second.empty()) memcpy(out, it->second.data(), it->second.size());
    return out;
}

char* aiml_step_workspace(void) {
    if (!g_cur.active) { LOG_ERROR("step.workspace: no running step"); return aiml_str_alloc(0); }
    char* out = aiml_str_alloc(g_cur.workspace.size());
    if (!g_cur.workspace.empty()) memcpy(out, g_cur.workspace.data(), g_cur.workspace.size());
    return out;
}

char* aiml_step_host(const char* name) {
    if (!g_cur.active) { LOG_ERROR("step.host: no running step"); return aiml_str_alloc(0); }
    auto it = g_cur.host.find(name ? name : "");
    const std::string val = (it != g_cur.host.end()) ? it->second : std::string();
    char* out = aiml_str_alloc(val.size());
    if (!val.empty()) memcpy(out, val.data(), val.size());
    return out;
}

int32_t aiml_pipeline_run(void* p) { return aiml_pipeline_run_observed(p, nullptr); }

int32_t aiml_pipeline_run_observed(void* p, void* observer_raw) {
    auto observer = reinterpret_cast<AimlStepObserver>(observer_raw);
    if (!p) return -2;
    auto* pp = static_cast<PPipeline*>(p);
    // spec/62 F62-124a: COMPILE distributed keys, but do not RUN them
    // locally — here, before the first step, with reasoning and the way.
    if (int32_t refused = aiml_pipeline_local_refusal(p)) return refused;
    pp->reused   = 0;
    pp->ran      = 0;
    pp->attempts = 0;
    pp->status   = -1;
    pp->lifecycle = 1;   // running

    // ── F62-84: Struktur VOLLSTAENDIG pruefen, bevor irgendetwas laeuft ──
    std::set<std::string> axes;
    for (const auto& pr : pp->params) axes.insert(pr.first);
    // F62-9: step names are unique, and `needs` only names EARLIER
    // steps.  Both together make the graph a DAG by construction —
    // which is why no cycle check is needed at runtime.
    {
        std::set<std::string> seen;
        for (const auto& stg : pp->stages) {
            for (const auto& st : stg.steps) {
                for (const auto& need : st.needs) {
                    if (!seen.count(need)) {
                        LOG_ERROR("pipeline '" + pp->name + "': step '" + st.name +
                                  "' needs '" + need + "' — that is not an earlier "
                                  "declared step (spec/62 F62-9)");
                        return -1;
                    }
                }
                if (!seen.insert(st.name).second) {
                    LOG_ERROR("pipeline '" + pp->name + "': the step name '" + st.name +
                              "' occurs twice — then it cannot be decided which "
                              "step a 'needs' means (spec/62 F62-9)");
                    return -1;
                }
            }
        }
    }
    for (const auto& stg : pp->stages) {
        for (const auto& st : stg.steps) {
            if (!st.body) {
                LOG_ERROR("pipeline '" + pp->name + "': step '" + st.name + "' has no body");
                return -1;
            }
            // F62-16: 'resolved' without a probe is pointless — the key would have
            // nothing that resolves the moving name, and the step would hit
            // the same entry forever.
            if (st.cache == 1 && !st.resolve) {
                LOG_ERROR("pipeline '" + pp->name + "': step '" + st.name +
                          "' is 'resolved', but has no probe (set_resolve)");
                return -1;
            }
            // F62-16, second half: 'pure' MUST NOT have a probe.  A
            // probe resolves a moving name — a pure step has
            // none, it only depends on hashed inputs.  Allowing both at once
            // would mean accepting two different justifications for the same
            // key, and hides which of the two applies.
            // F62-103: `pure` without a checked body is a WARNING, not an
            // error.  The runtime cannot verify the claim (the
            // body is a bundle); aborting a run because a
            // CHECK is missing would make the pipeline hostage to a setting that
            // the author may have deliberately left out.
            if (st.cache == 2 && !aiml_code_is_pure(st.body)) {
                LOG_WARNING("pipeline '" + pp->name + "': step '" + st.name +
                            "' is 'pure', but its body was not checked with "
                            "sys.bundle(..., pure: true) — 'pure' holds here "
                            "unproven (spec/62 F62-103)");
            }
            // F62-115: a `per: once` step has NO lane bindings — there is
            // one result for the whole matrix.  A declared axis
            // would be a result that depends on the order of the lanes.
            if (st.per == 1 && !st.args.empty()) {
                LOG_ERROR("pipeline '" + pp->name + "': step '" + st.name +
                          "' is 'per: once' and still declares axes — a "
                          "one-off step cannot depend on an axis "
                          "(spec/62 F62-115)");
                return -1;
            }
            if (st.cache == 2 && st.resolve) {
                LOG_ERROR("pipeline '" + pp->name + "': step '" + st.name +
                          "' is 'pure' and still has a probe — a probe "
                          "belongs to 'resolved'");
                return -1;
            }
            // F62-41: retry with 'pure' is a CONTRADICTION.  A pure step
            // only depends on hashed inputs, so a second attempt must
            // fail just the same — whoever asks for one says two things at once.
            if (st.retry > 1 && st.cache == 2) {
                LOG_ERROR("pipeline '" + pp->name + "': step '" + st.name +
                          "' is 'pure' and asks for retry=" + std::to_string(st.retry) +
                          " — a pure step fails reproducibly");
                return -1;
            }
            for (const auto& a : st.args) {
                if (!axes.count(a)) {
                    LOG_ERROR("pipeline '" + pp->name + "': step '" + st.name +
                              "' uses the axis '" + a + "', which the pipeline does not have");
                    return -1;
                }
            }
        }
    }

    // ── F62-4: every exclude entry must name an axis AND a value of its
    //    domain.  A typo would otherwise silently exclude NOTHING,
    //    and the matrix would run larger than intended without anyone noticing.
    for (const auto& ex : pp->excludes) {
        for (const auto& kv : ex) {
            const std::vector<std::string>* dom = nullptr;
            for (const auto& pr : pp->params) if (pr.first == kv.first) { dom = &pr.second; break; }
            if (!dom) {
                LOG_ERROR("pipeline '" + pp->name + "': exclude names the axis '" + kv.first +
                          "', which the pipeline does not have");
                return -1;
            }
            bool found = false;
            for (const auto& v : *dom) if (v == kv.second) { found = true; break; }
            if (!found) {
                LOG_ERROR("pipeline '" + pp->name + "': exclude names for '" + kv.first +
                          "' the value '" + kv.second + "', which is not in its domain");
                return -1;
            }
        }
    }

    // ── Expand and execute the lanes ────────────────────────────────────
    std::vector<Bindings> all_lanes;
    expand(pp->params, 0, Bindings{}, all_lanes);
    std::vector<Bindings> lanes;
    for (const auto& b : all_lanes) if (!excluded(pp->excludes, b)) lanes.push_back(b);

    // F62-5: everything excluded is an error, not an empty success report.
    if (lanes.empty()) {
        LOG_ERROR("pipeline '" + pp->name + "': exclude excludes EVERY lane — nothing to do");
        return -1;
    }

    // ── §6: lanes are parallel by construction ─────────────────────────
    //
    // Two locks, and the separation is the point:
    //   * `mu` protects cache, counters, status — and every call into
    //     USER CODE (observer, probe).  That was not written thread-safe
    //     and should not have to be (F62-100).
    //   * `compile_mu` serialises ONLY the compilation of a body.  Whether
    //     several isolated compilations may run at the same time is a
    //     question for LLVM and is NOT answered by the measurement of F62-14
    //     (F62-99).  Until then: lanes in parallel, compilation not.
    struct RunState {
        std::mutex mu;
        std::mutex compile_mu;
        bool    stop_all = false;   // on_fail=pipeline or pause
        int32_t stop_rc  = 0;
        bool    paused   = false;
        int32_t worst_rc = 0;
        // F62-114: the key of EVERY step that ran, by (name, lane).
        // Lane -1 stands for a `per: once` step, because there is only one
        // there.  A successor forms its `input_keys` from this.
        std::map<std::pair<std::string, int32_t>, std::string> keyOf;
        // F62-117: steps that are skipped because a step they
        // (transitively) need has failed.  Skipped is NOT
        // failed: no outcome, no record.
        std::set<std::string> skipped;
        // §13.5h: the result of every step that ran OR hit.
        // On a hit it comes from the cache (F62-120) — for the
        // successor both are the same, and that is exactly the point.
        std::map<std::pair<std::string, int32_t>, std::string> resultOf;
    };
    RunState state;

    // One pass over a SELECTION of steps.  `lane_idx == -1` means
    // `per: once`: a pass without lane bindings (§5.5).
    using StepRef = std::pair<PStage*, PStep*>;
    auto run_steps = [&](const Bindings& bind, int32_t lane_idx,
                         const std::vector<StepRef>& plan) {
        std::string btxt = bind_text(bind);
        {
            for (auto& ref : plan) {
                PStage& stg = *ref.first;
                PStep&  st  = *ref.second;
                {   // Has another lane stopped the dispatch?
                    std::lock_guard<std::mutex> g(state.mu);
                    if (state.stop_all) return;
                }
                // F62-117: does this step need something that has failed?
                // Then it does not start.  Before everything else, even before the
                // probe — a skipped step resolves nothing.
                {
                    std::lock_guard<std::mutex> g(state.mu);
                    if (state.skipped.count(st.name)) continue;
                    bool blocked = false;
                    for (const auto& need : st.needs)
                        if (state.skipped.count(need)) { blocked = true; break; }
                    if (blocked) {
                        state.skipped.insert(st.name);
                        LOG_WARNING("pipeline '" + pp->name + "': step '" + st.name +
                                    "' skipped — it needs a failed "
                                    "step (spec/62 F62-117)");
                        continue;
                    }
                }
                // §5.9 / F62-54: the declared gate.  Before the probe, because a
                // gate means "no further work here" — not even
                // resolving.
                if (st.pause_before && !pp->released.count(st.name)) {
                    std::lock_guard<std::mutex> g(state.mu);
                    LOG_INFO("pipeline '" + pp->name + "': paused before step '" +
                             st.name + "' (pause_before)");
                    state.paused   = true;
                    state.stop_all = true;
                    state.stop_rc  = -6;
                    return;
                }
                // The probe ALWAYS runs — even if the step is about to hit.
                // That is exactly its purpose: only its result decides WHETHER it
                // hits (§9).  Whoever skips it on a hit has a moving
                // name in the key again.  User code, hence
                // under `mu`.
                std::string resolved;
                if (st.resolve) {
                    std::lock_guard<std::mutex> g(state.mu);
                    auto fn = reinterpret_cast<AimlResolveFn>(st.resolve);
                    const char* out = fn();
                    if (out) resolved = out;
                }
                // F62-114: the keys of the `needs` go as `input_keys`
                // into the own key (§8) — that way a change further up
                // propagates transitively downwards, and a chain of
                // steps behaves like a stack of layers.
                std::string inputKeys;
                if (!st.needs.empty()) {
                    std::vector<std::string> ks;
                    {
                        std::lock_guard<std::mutex> g(state.mu);
                        for (const auto& need : st.needs) {
                            auto it = state.keyOf.find({need, lane_idx});
                            if (it == state.keyOf.end())
                                it = state.keyOf.find({need, -1});
                            ks.push_back(it != state.keyOf.end() ? it->second : std::string());
                        }
                    }
                    std::sort(ks.begin(), ks.end());   // §8: sortiert
                    for (const auto& k : ks)
                        inputKeys += std::to_string(k.size()) + ":" + k;
                }
                std::string key = step_key(st, bind, resolved) + inputKeys;
                {
                    std::lock_guard<std::mutex> g(state.mu);
                    state.keyOf[{st.name, lane_idx}] = key;
                }
                int32_t rc     = 0;
                int32_t reused = 0;
                int     reach  = st.on_fail;

                bool hit = false;
                std::string hitResult;
                if (st.cache != 0) {
                    std::lock_guard<std::mutex> g(state.mu);
                    auto it = pp->cache.find(key);
                    if (it != pp->cache.end()) {
                        hit       = true;
                        rc        = it->second.rc;
                        hitResult = it->second.result;
                    }
                }
                if (hit) {   // opaque never hits (§7)
                    reused = 1;
                    std::lock_guard<std::mutex> g(state.mu);
                    ++pp->reused;
                    // F62-120: the hit restores the result
                    // without calling the body.
                    state.resultOf[{st.name, lane_idx}] = hitResult;
                } else {
                    // F62-41: up to `retry` attempts, stopping at the first one that
                    // succeeds.  ALL attempts are counted (attempts), because
                    // otherwise `retry` would not be observable.
                    // §4.3 / F62-60: one workspace per (lane, stage), formed from
                    // their identity — that way concurrent
                    // lanes cannot see or spoil each other.  Absolute,
                    // so that `cwd` in the body changes nothing about it (F62-30: there
                    // is no parent-side chdir, and that is the reason
                    // why lanes may run in threads at all).
                    std::string ws_root = pp->workspace_root;
                    if (!ws_root.empty() && ws_root[0] != '/') {
                        char cwdbuf[4096];
                        if (getcwd(cwdbuf, sizeof(cwdbuf)))
                            ws_root = std::string(cwdbuf) + "/" + ws_root;
                    }
                    std::string ws = ws_root + "/" + pp->name + "/" +
                                     (btxt.empty() ? std::string("_") : btxt) + "/" + stg.name;
                    aiml_io_mkdir_all(ws.c_str());

                    for (int32_t attempt = 1; attempt <= st.retry; ++attempt) {
                        g_cur = CurStep{};
                        g_cur.active    = true;
                        g_cur.bindings  = bind;
                        g_cur.workspace = ws;
                        for (const auto& axis : st.args) g_cur.declared.insert(axis);
                        // §13.5h: only the DECLARED needs are readable —
                        // the same boundary the key draws (F62-119).
                        {
                            std::lock_guard<std::mutex> g(state.mu);
                            for (const auto& need : st.needs) {
                                auto rit = state.resultOf.find({need, lane_idx});
                                if (rit == state.resultOf.end())
                                    rit = state.resultOf.find({need, -1});
                                g_cur.need_results[need] =
                                    (rit != state.resultOf.end()) ? rit->second : std::string();
                            }
                        }
                        auto t0 = std::chrono::steady_clock::now();
                        {
                            std::lock_guard<std::mutex> g(state.compile_mu);
                            rc = aiml_exec_code(st.body);
                        }
                        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                           std::chrono::steady_clock::now() - t0).count();
                        {
                            std::lock_guard<std::mutex> g(state.mu);
                            ++pp->attempts;
                        }
                        // F62-40: the limit applies to the WHOLE step.  Here
                        // it is detected AFTERWARDS, not enforced:
                        // aborting the body in the middle of the run would mean killing JIT code
                        // in a foreign thread.  Killing the
                        // children (process.kill, spec/23 §5.2) belongs to the
                        // day on which the limit is enforced; until then it is
                        // a finding and not an interruption — a
                        // difference that must be in the log, so that nobody
                        // relies on an interruption.
                        if (st.timeout_ms > 0 && elapsed > st.timeout_ms) {
                            LOG_ERROR("pipeline '" + pp->name + "': step '" + st.name +
                                      "' exceeded the time limit (" + std::to_string(elapsed) +
                                      " ms > " + std::to_string(st.timeout_ms) +
                                      " ms) — the run was NOT interrupted (§5.6, F62-40)");
                            if (rc == 0) rc = -4;
                        }
                        // What the body itself said (F62-90).  g_cur is
                        // thread-local, so every lane only sees its own.
                        if (g_cur.status_set) st.status = g_cur.status;
                        if (g_cur.result_set) {
                            std::lock_guard<std::mutex> g(state.mu);
                            state.resultOf[{st.name, lane_idx}] = g_cur.result;
                        }
                        if (g_cur.fail_reach >= 0) {
                            // F62-46: an explicit abort beats the
                            // step's default — the body knows more at this
                            // point than the declaration.
                            reach = (g_cur.fail_reach == 1) ? 1 : 0;
                            if (rc == 0) rc = -5;
                            LOG_ERROR("pipeline '" + pp->name + "': step '" + st.name +
                                      "' brach ab: " + g_cur.fail_msg);
                        }
                        g_cur = CurStep{};
                        if (rc == 0) break;
                        if (attempt < st.retry)
                            LOG_WARNING("pipeline '" + pp->name + "': step '" + st.name +
                                        "' Versuch " + std::to_string(attempt) + " von " +
                                        std::to_string(st.retry) + " endete mit " + std::to_string(rc));
                    }
                    std::lock_guard<std::mutex> g(state.mu);
                    if (rc == 0) ++pp->ran;
                    // §7 / F62-92: ONLY a successful attempt is recorded.
                    // Recording a failure would mean repeating it for every
                    // further lane without having tried it.
                    if (st.cache != 0 && rc == 0) {
                        PPipeline::CacheEntry entry;
                        entry.rc = rc;
                        auto rit = state.resultOf.find({st.name, lane_idx});
                        if (rit != state.resultOf.end()) entry.result = rit->second;
                        pp->cache[key] = entry;
                    }
                }

                {
                    std::lock_guard<std::mutex> g(state.mu);
                    // Automatic colour if the body has not set one.
                    // Which colour means what is open (§16.6) — that is why
                    // only the assignment stands here, in ONE place: 1 (green)
                    // succeeded, 4 (cyan) hit, 0 (red) failed.
                    int32_t st_status = (st.status >= 0) ? st.status
                                                         : ((rc != 0) ? 0 : (reused ? 4 : 1));
                    st.status  = st_status;
                    pp->status = status_max(pp->status, st_status);

                    // Observer BEFORE error handling: a failed
                    // step is what the host most urgently wants to see
                    // (F62-78).  It gets a copy, not a ref<> (F62-80).
                    // Serialised (F62-100): the order is then not
                    // the source order, but every call is on its own.
                    if (observer) {
                        AimlStepRun ev{};
                        ev.name      = st.name.c_str();
                        ev.bindings  = btxt.c_str();
                        ev.step_key  = key.c_str();
                        ev.lane      = lane_idx;
                        ev.reused    = reused;
                        ev.exit_code = rc;
                        observer(&ev);
                    }

                    // F62-55: a pause requested from outside takes effect AFTER
                    // this step — the workspace is now a
                    // complete tree, and the key is recorded.
                    if (pp->pause_wanted && rc == 0) {
                        LOG_INFO("pipeline '" + pp->name + "': paused after step '" +
                                 st.name + "'");
                        state.paused   = true;
                        state.stop_all = true;
                        state.stop_rc  = -6;
                        return;
                    }

                    if (rc != 0) {
                        LOG_ERROR("pipeline '" + pp->name + "': step '" + st.name +
                                  "' endete mit " + std::to_string(rc));
                        // F62-39: the reach is on the step, not on the
                        // pipeline — a failed fetch_sources makes
                        // every lane pointless, a failed build in one
                        // lane says nothing about the others.
                        if (reach == 1) {           // pipeline: nothing starts any more
                            state.stop_all = true;
                            state.stop_rc  = rc;
                            return;
                        }
                        if (reach == 0) {           // lane
                            state.worst_rc = rc;
                            // F62-117: for a `per: once` step there is
                            // no single lane that could end — instead
                            // exactly its dependants do not start.
                            if (st.per == 1) {
                                state.skipped.insert(st.name);
                                continue;
                            }
                            return;
                        }
                        // proceed: noted, the lane keeps running — the run
                        // itself therefore does NOT count as failed.
                    }
                }
            }
        }
    };

    int32_t limit = pp->parallel;
    if (limit == 0) {
        unsigned hw = std::thread::hardware_concurrency();
        limit = (hw > 0) ? static_cast<int32_t>(hw) : 1;
    }
    if (limit > static_cast<int32_t>(lanes.size())) limit = static_cast<int32_t>(lanes.size());

    // ── §5.5 / F62-116: sections, split at every `per: once` step ──
    //
    // A `per: once` step is a BARRIER: all lanes reach it, it
    // runs exactly once, after that the lanes keep running.  Between two
    // barriers lane parallelism stays (§6) — there is only waiting where
    // something shared comes into being.
    //
    // The plan is formed in declaration order.  That is enough for
    // `needs`, because F62-9 only allows EARLIER steps: whoever comes first in the order
    // is finished before its successor begins.
    struct Segment {
        bool                 once = false;
        std::vector<StepRef> steps;   // exactly one for once
    };
    std::vector<Segment> segments;
    for (auto& stg : pp->stages) {
        for (auto& st : stg.steps) {
            if (st.per == 1) {
                Segment seg;
                seg.once = true;
                seg.steps.push_back({&stg, &st});
                segments.push_back(std::move(seg));
            } else {
                if (segments.empty() || segments.back().once) segments.push_back(Segment{});
                segments.back().steps.push_back({&stg, &st});
            }
        }
    }

    auto run_lane_segment = [&](const std::vector<StepRef>& plan) {
        if (limit <= 1) {
            for (size_t i = 0; i < lanes.size(); ++i) {
                if (state.stop_all) break;
                run_steps(lanes[i], static_cast<int32_t>(i), plan);
            }
            return;
        }
        // Division of labour via a shared index: that way a lane
        // that takes long does not get seven more saddled on it while other
        // threads run idle.
        std::atomic<size_t> next{0};
        std::vector<std::thread> workers;
        workers.reserve(static_cast<size_t>(limit));
        for (int32_t w = 0; w < limit; ++w) {
            workers.emplace_back([&]() {
                for (;;) {
                    size_t i = next.fetch_add(1);
                    if (i >= lanes.size()) return;
                    {
                        std::lock_guard<std::mutex> g(state.mu);
                        if (state.stop_all) return;
                    }
                    run_steps(lanes[i], static_cast<int32_t>(i), plan);
                }
            });
        }
        for (auto& t : workers) t.join();
    };

    for (const auto& seg : segments) {
        {
            std::lock_guard<std::mutex> g(state.mu);
            if (state.stop_all) break;
        }
        if (seg.once) {
            // Once, without lane bindings and single-threaded: the barrier itself
            // is the place where nothing should run alongside.
            run_steps(Bindings{}, -1, seg.steps);
        } else {
            run_lane_segment(seg.steps);
        }
    }

    pp->lifecycle = state.paused ? 2 : 3;
    return (state.stop_rc != 0) ? state.stop_rc : state.worst_rc;
}

// ── spec/62 §20: READING the compiled pipeline ──────────────────────────────
//
// Written by the rewrite (pipeline_expand), read by the host.
// Both go through ONE key name per field, so that the names in the spec,
// the rewrite and the reader cannot drift apart.

static std::string* stage_text(PStage* g, const std::string& key) {
    if (key == "name")       return &g->name;
    if (key == "agent")      return &g->runs_on;
    if (key == "steplayer")  return &g->steplayer;
    if (key == "image")      return &g->image;
    if (key == "base")       return &g->base;
    if (key == "requires")   return &g->requires_on;
    if (key == "shares")     return &g->shares;
    if (key == "command")    return &g->command;
    return nullptr;
}

static int32_t* stage_num(PStage* g, const std::string& key) {
    if (key == "cpu")     return &g->cpu_cores;
    if (key == "mem")     return &g->mem_mb;
    if (key == "pids")    return &g->pids_max;
    if (key == "timeout") return &g->timeout_ms;
    return nullptr;
}

static char* str_out(const std::string& val) {
    char* out = aiml_str_alloc(val.size());
    if (!val.empty()) memcpy(out, val.data(), val.size());
    return out;
}

// Lanes of this pipeline, `exclude` already subtracted (§4.3).
static std::vector<Bindings> live_lanes(PPipeline* pp) {
    std::vector<Bindings> all;
    expand(pp->params, 0, Bindings{}, all);
    std::vector<Bindings> out;
    for (const auto& b : all) if (!excluded(pp->excludes, b)) out.push_back(b);
    return out;
}

void aiml_stage_set_attr(void* g, const char* key, const char* val) {
    if (!g || !key || !val) return;
    if (std::string* slot = stage_text(static_cast<PStage*>(g), key)) *slot = val;
    else LOG_ERROR(std::string("stage.set_attr: unbekannter Schluessel '") + key + "'");
}

void aiml_stage_set_limit(void* g, const char* key, int32_t value) {
    if (!g || !key) return;
    if (int32_t* slot = stage_num(static_cast<PStage*>(g), key)) *slot = value;
    else LOG_ERROR(std::string("stage.set_limit: unbekannter Schluessel '") + key + "'");
}

char* aiml_stage_attr(void* g, const char* key) {
    if (!g || !key) return aiml_str_alloc(0);
    std::string* slot = stage_text(static_cast<PStage*>(g), key);
    return str_out(slot ? *slot : std::string());
}

int32_t aiml_stage_limit(void* g, const char* key) {
    if (!g || !key) return 0;
    int32_t* slot = stage_num(static_cast<PStage*>(g), key);
    return slot ? *slot : 0;
}

int32_t aiml_stage_step_count(void* g) {
    return g ? (int32_t)static_cast<PStage*>(g)->steps.size() : 0;
}

// nullptr for an index out of range: the caller (builtin_registry) then throws.
void* aiml_stage_step_at(void* g, int32_t idx) {
    if (!g) return nullptr;
    auto* st = static_cast<PStage*>(g);
    if (idx < 0 || idx >= (int32_t)st->steps.size()) return nullptr;
    return &st->steps[(size_t)idx];
}

char* aiml_step_attr(void* s, const char* key) {
    if (!s || !key) return aiml_str_alloc(0);
    auto* st = static_cast<PStep*>(s);
    const std::string k(key);
    if (k == "name")    return str_out(st->name);
    if (k == "cache")   return str_out(st->cache == 2 ? "pure" : (st->cache == 1 ? "resolved" : "opaque"));
    if (k == "per")     return str_out(st->per == 1 ? "once" : "lane");
    return str_out(std::string());
}

int32_t aiml_step_needs_count(void* s) {
    return s ? (int32_t)static_cast<PStep*>(s)->needs.size() : 0;
}

char* aiml_step_need_at(void* s, int32_t idx) {
    if (!s) return aiml_str_alloc(0);
    auto* st = static_cast<PStep*>(s);
    if (idx < 0 || idx >= (int32_t)st->needs.size()) {
        LOG_ERROR("step.need_at: Index " + std::to_string(idx) + " liegt ausserhalb");
        return aiml_str_alloc(0);
    }
    return str_out(st->needs[(size_t)idx]);
}

void aiml_pipeline_add_push(void* p, const char* from, const char* to, const char* tag) {
    if (!p || !from || !to) return;
    PPush push;
    push.from = from;
    push.to   = to;
    push.tag  = tag ? tag : "";
    static_cast<PPipeline*>(p)->pushes.push_back(push);
}

char* aiml_pipeline_name_of(void* p) {
    return p ? str_out(static_cast<PPipeline*>(p)->name) : aiml_str_alloc(0);
}

int32_t aiml_pipeline_stage_count(void* p) {
    return p ? (int32_t)static_cast<PPipeline*>(p)->stages.size() : 0;
}

void* aiml_pipeline_stage_at(void* p, int32_t idx) {
    if (!p) return nullptr;
    auto* pp = static_cast<PPipeline*>(p);
    if (idx < 0 || idx >= (int32_t)pp->stages.size()) return nullptr;
    return &pp->stages[(size_t)idx];
}

// The binding text of the i-th lane — the same form as in the observer (F62-152).
char* aiml_pipeline_lane_bindings(void* p, int32_t idx) {
    if (!p) return aiml_str_alloc(0);
    std::vector<Bindings> lanes = live_lanes(static_cast<PPipeline*>(p));
    if (idx < 0 || idx >= (int32_t)lanes.size()) {
        LOG_ERROR("pipeline.lane_bindings: Index " + std::to_string(idx) + " liegt ausserhalb");
        return aiml_str_alloc(0);
    }
    return str_out(bind_text(lanes[(size_t)idx]));
}

int32_t aiml_pipeline_axis_count(void* p) {
    return p ? (int32_t)static_cast<PPipeline*>(p)->params.size() : 0;
}

char* aiml_pipeline_axis_name(void* p, int32_t idx) {
    if (!p) return aiml_str_alloc(0);
    auto* pp = static_cast<PPipeline*>(p);
    if (idx < 0 || idx >= (int32_t)pp->params.size()) {
        LOG_ERROR("pipeline.axis_name: Index " + std::to_string(idx) + " liegt ausserhalb");
        return aiml_str_alloc(0);
    }
    return str_out(pp->params[(size_t)idx].first);
}

char* aiml_pipeline_axis_type(void* p, int32_t idx) {
    if (!p) return aiml_str_alloc(0);
    auto* pp = static_cast<PPipeline*>(p);
    if (idx < 0 || idx >= (int32_t)pp->params.size()) return aiml_str_alloc(0);
    auto it = pp->param_types.find(pp->params[(size_t)idx].first);
    return str_out(it == pp->param_types.end() ? std::string("string") : it->second);
}

int32_t aiml_pipeline_axis_value_count(void* p, int32_t idx) {
    if (!p) return 0;
    auto* pp = static_cast<PPipeline*>(p);
    if (idx < 0 || idx >= (int32_t)pp->params.size()) return 0;
    return (int32_t)pp->params[(size_t)idx].second.size();
}

char* aiml_pipeline_axis_value(void* p, int32_t axis, int32_t vix) {
    if (!p) return aiml_str_alloc(0);
    auto* pp = static_cast<PPipeline*>(p);
    if (axis < 0 || axis >= (int32_t)pp->params.size()) return aiml_str_alloc(0);
    const auto& vals = pp->params[(size_t)axis].second;
    if (vix < 0 || vix >= (int32_t)vals.size()) {
        LOG_ERROR("pipeline.axis_value: Index " + std::to_string(vix) + " liegt ausserhalb");
        return aiml_str_alloc(0);
    }
    return str_out(vals[(size_t)vix]);
}

int32_t aiml_pipeline_push_count(void* p) {
    return p ? (int32_t)static_cast<PPipeline*>(p)->pushes.size() : 0;
}

char* aiml_pipeline_push_field(void* p, int32_t idx, const char* which) {
    if (!p || !which) return aiml_str_alloc(0);
    auto* pp = static_cast<PPipeline*>(p);
    if (idx < 0 || idx >= (int32_t)pp->pushes.size()) {
        LOG_ERROR("pipeline.push_*: Index " + std::to_string(idx) + " liegt ausserhalb");
        return aiml_str_alloc(0);
    }
    const PPush& push = pp->pushes[(size_t)idx];
    const std::string key(which);
    if (key == "from") return str_out(push.from);
    if (key == "to")   return str_out(push.to);
    return str_out(push.tag);
}

// spec/62 F62-124a: this is where the rejection happens — at RUN time, not at
// compile time.  The message names three things: what is requested, that there is
// no runtime here, and the way to get there (F62-124).
int32_t aiml_pipeline_local_refusal(void* p) {
    if (!p) return 0;
    auto* pp = static_cast<PPipeline*>(p);
    for (const auto& stg : pp->stages) {
        if (!stg.image.empty()) {
            LOG_ERROR("pipeline '" + pp->name + "': stage '" + stg.name + "' requests the image '"
                      + stg.image + "' (spec/62 §18.1) — a local run has no runtime "
                      "that provides a root.  The way: run it through a host that can do "
                      "containers (cosmos doc/spec/110).");
            return -4;
        }
    }
    if (!pp->pushes.empty()) {
        LOG_ERROR("pipeline '" + pp->name + "': `push:` requires a place to which an image "
                  "is written (spec/62 §18.1) — a local run has none.  The way: run it through "
                  "a host that can do containers (cosmos doc/spec/110).");
        return -4;
    }
    return 0;
}

} // extern "C"
