#ifndef AIML_PIPELINE_RUNTIME_H
#define AIML_PIPELINE_RUNTIME_H

#include <cstdint>

// spec/62 §13.5: pipeline / stage / step as composable values.
//
// The BODY of a step is a code bundle (F62-85) — i.e. compiled,
// checked source text with a content address.  Only the STRUCTURE is
// composed: which stages, which axes, which bindings.  That is why the
// purity check (on the body) and the content-derived key are preserved.
extern "C" {

void*   aiml_step_new(void);
void    aiml_step_set_name(void* step, const char* name);
void    aiml_step_set_body(void* step, void* code);       // Code*
void    aiml_step_set_cache(void* step, int32_t cls);
void    aiml_step_use_arg(void* step, const char* axis);
// spec/62 §9: the probe.  It runs BEFORE the key is formed, EVERY time,
// and its result goes into the key — that way a moving name
// (branch, package version) becomes a fixed address.
typedef const char* (*AimlResolveFn)(void);
void    aiml_step_set_resolve(void* step, void* fn);
// spec/62 §5.6: reach of a failure, time limit, attempts.  None
// of the three is key material (F62-91) — all describe the FAILURE,
// only a successful attempt is recorded.
void    aiml_step_set_on_fail(void* step, int32_t reach);   // 0=lane 1=pipeline 2=continue
void    aiml_step_set_timeout(void* step, int32_t ms);      // 0 = no limit
void    aiml_step_set_retry(void* step, int32_t tries);
int32_t aiml_step_status(void* step);
// spec/62 §5.9: deklariertes Tor (F62-54).  Kein Schluesselmaterial (F62-96).
void    aiml_step_set_pause_before(void* step, int32_t on);
// spec/62 §5.5: `per: lane` (0, default) or `per: once` (1).  Not
// key material, but it limits what may be in the key (F62-115).
void    aiml_step_set_per(void* step, int32_t per);
// spec/62 §5.1/F62-9: only earlier steps.  Key material via
// input_keys (F62-114) — order and failure reach, no data flow
// (F62-113).
void    aiml_step_add_need(void* step, const char* name);

// spec/62 §5.7 / F62-90: the body sets its own outcome.  Effect only
// while a step is running (thread-local, lanes may run in threads).
void    aiml_step_status_set(int32_t status);
void    aiml_step_fail_lane(const char* msg);
void    aiml_step_fail_pipeline(const char* msg);
// spec/62 §13.5e: how a body sees its lane.  From the same thread-local
// place as the status — a bundle carries no state of the
// caller (F62-89).  Return values are refcounted aiml strings.
char*   aiml_step_arg(const char* axis);
char*   aiml_step_workspace(void);
char*   aiml_step_host(const char* name);
// spec/62 §13.5h: result channel.  Publish and read; only a DECLARED need
// may be read (F62-119).
void    aiml_step_set_result(const char* value);
char*   aiml_step_need_result(const char* name);

void*   aiml_stage_new(void);
void    aiml_stage_set_name(void* stage, const char* name);
void    aiml_stage_set_runs_on(void* stage, const char* label);
void    aiml_stage_add_step(void* stage, void* step);

void*   aiml_pipeline_new(void);
void    aiml_pipeline_set_name(void* pipe, const char* name);
// spec/62 §4.1: axis WITH type ([name, type, [values]]).  The type counts in the
// body (F62-1) and is therefore part of the shape a host reads.
void    aiml_pipeline_add_param(void* pipe, const char* axis, const char* type,
                                const void* values); // values: list<string>
void    aiml_pipeline_add_stage(void* pipe, void* stage);
// spec/62 §4.2: partial binding as text, "axis=value[,axis=value...]".
// Text form, because a partial binding has the same format as the observer's
// `bindings` (§13.1) — one format for the same concept, instead of two.
void    aiml_pipeline_add_exclude(void* pipe, const char* partial);
int32_t aiml_pipeline_lane_count(void* pipe);
// Observer: called per (step, lane).  The fields correspond to the
// StepRun struct on the aiml side (spec/62 §13.1) — the order MUST match.
struct AimlStepRun {
    const char* name;
    const char* bindings;    // "arch=arm64,mode=debug"
    const char* step_key;
    int32_t     lane;
    int32_t     reused;      // 1 = hit, body did not run
    int32_t     exit_code;
};
typedef void (*AimlStepObserver)(struct AimlStepRun*);

int32_t aiml_pipeline_run(void* pipe);
int32_t aiml_pipeline_run_observed(void* pipe, void* observer);
int32_t aiml_pipeline_reused_count(void* pipe);
int32_t aiml_pipeline_ran_count(void* pipe);
// Attempts including the failed ones (§5.6): only that way is `retry`
// measurable at all — ran_count counts successful executions.
int32_t aiml_pipeline_attempt_count(void* pipe);
int32_t aiml_pipeline_status(void* pipe);
// spec/62 §5.9: stopping at the step boundary, resuming from the cache (F62-95).
void    aiml_pipeline_request_pause(void* pipe);
void    aiml_pipeline_release(void* pipe, const char* step_name);
void    aiml_pipeline_resume(void* pipe);
int32_t aiml_pipeline_lifecycle(void* pipe);   // 0 pending 1 running 2 paused 3 done
// spec/62 §6: gleichzeitige Lanes; 0 = auto.  Kein Schluesselmaterial (F62-98).
void    aiml_pipeline_set_parallel(void* pipe, int32_t n);
// spec/62 §4.3: one workspace per (lane, stage).  Not key material
// (F62-107) — otherwise no hit would survive moving the project.
void    aiml_pipeline_set_workspace_root(void* pipe, const char* root);
void    aiml_pipeline_set_store(void* pipe, const char* dir);    // spec/62 F62-24

// ── spec/62 §20: reading what was compiled ──────────────────────────────────
//
// Written by the rewrite (`pipeline_expand`), read by a
// host.  ONE key name per field, so that spec, rewrite and reader do not
// drift apart: "name", "agent", "steplayer", "image", "base",
// "requires", "shares", "command" as text; "cpu", "mem", "pids", "timeout"
// as a number (0 = host default, NOT unlimited).
//
// The values are RESOLVED (F62-151): the rewrite has already applied the
// stage-over-pipeline precedence.  A reader that recomputes is a second
// parser (§20.1).
void    aiml_stage_set_attr(void* stage, const char* key, const char* value);
void    aiml_stage_set_limit(void* stage, const char* key, int32_t value);
char*   aiml_stage_attr(void* stage, const char* key);
int32_t aiml_stage_limit(void* stage, const char* key);
int32_t aiml_stage_step_count(void* stage);
// nullptr on a bad index — the caller then throws (F62-154).
void*   aiml_stage_step_at(void* stage, int32_t idx);

char*   aiml_step_attr(void* step, const char* key);   // name | cache | per
int32_t aiml_step_needs_count(void* step);
char*   aiml_step_need_at(void* step, int32_t idx);

void    aiml_pipeline_add_push(void* pipe, const char* from, const char* to, const char* tag);
char*   aiml_pipeline_name_of(void* pipe);
int32_t aiml_pipeline_stage_count(void* pipe);
void*   aiml_pipeline_stage_at(void* pipe, int32_t idx);
// Binding text of the i-th lane, with `exclude` already subtracted — the same form as
// in the observer and in the workspace name (F62-152).
char*   aiml_pipeline_lane_bindings(void* pipe, int32_t idx);
int32_t aiml_pipeline_axis_count(void* pipe);
char*   aiml_pipeline_axis_name(void* pipe, int32_t idx);
char*   aiml_pipeline_axis_type(void* pipe, int32_t idx);
int32_t aiml_pipeline_axis_value_count(void* pipe, int32_t idx);
char*   aiml_pipeline_axis_value(void* pipe, int32_t axis, int32_t vix);
int32_t aiml_pipeline_push_count(void* pipe);
char*   aiml_pipeline_push_field(void* pipe, int32_t idx, const char* which); // from|to|tag

// spec/62 F62-124a: 0 = runnable locally, -4 = rejected (the message is in the
// log and names three things).  `aiml_pipeline_run` calls it itself.
int32_t aiml_pipeline_local_refusal(void* pipe);

} // extern "C"
#endif
