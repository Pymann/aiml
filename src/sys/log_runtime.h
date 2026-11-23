#pragma once
#ifdef __cplusplus
extern "C" {
#endif

/* ── Leveled logging (Spec 46) ───────────────────────────────────────────── */

void aiml_log_verbose(const char* msg, const char* loc);
void aiml_log_debug(const char* msg, const char* loc);
void aiml_log_info(const char* msg, const char* loc);
void aiml_log_warn(const char* msg, const char* loc);
void aiml_log_error(const char* msg, const char* loc);
void aiml_log_fatal(const char* msg, const char* loc);       /* logs + exit(1) */
void aiml_log_deprecated(const char* msg, const char* loc);
void aiml_log_system(const char* msg, const char* loc);

void aiml_log_set_level(const char* level);   /* verbose/debug/info/warn/error/fatal */
void aiml_log_set_file(const char* path);     /* NULL or "" to disable file mirror   */

/* ── spec/22: message of the last throw ────────────────────────────────────────
 * Every `throw` site stores its `message` here, so that the top level
 * can report it if nobody catches. Belongs in the runtime, not in the
 * JIT host: AOT and wasm32 targets link the same throw path. */
void        aiml_note_thrown_message(const char* msg);
const char* aiml_last_thrown_message(void);

/* ── Diagnostic trace ring (env AIML_MODEL_TRACE) ─────────────────────────────
 * A lock-protected fixed-size ring of the most recent lifetime events for heap
 * objects, kept so that when a corruption is finally detected we can print the
 * full recent history of the offending pointer — the events that a plain crash
 * or a sanitizer-masked run would otherwise lose. Entirely inert (one cached
 * atomic read, no allocation, no locking) unless AIML_MODEL_TRACE is set.
 *
 * Level from AIML_MODEL_TRACE: "1" = record + loud on corruption; "2" (or more)
 * also asks subsystems to quarantine freed shells so a later dangling read hits
 * a deterministic poison pattern instead of reused memory. */
int  aiml_trace_level(void);   /* cached: 0 = off, else the integer value of AIML_MODEL_TRACE */

/* Record one event. `op` must be a string literal / static (stored by pointer);
 * `a`/`b` are op-specific numbers (e.g. magic, refcount); `note` is copied into a
 * small inline buffer and may be transient/NULL. No-op when tracing is off. */
void aiml_trace_record(const char* op, const void* ptr,
                       unsigned long a, unsigned long b, const char* note);

/* Dump (to the log + file mirror) every recorded event whose ptr matches, newest
 * last; pass NULL to dump the whole ring. Prefixed with `reason`. No-op when off. */
void aiml_trace_dump(const void* ptr, const char* reason);

#ifdef __cplusplus
}
#endif
