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

#ifdef __cplusplus
}
#endif
