#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

// ── Intern / normalize ────────────────────────────────────────────────────────
// Normalize raw path string and intern it into the global path table.
// Returns a stable const char* (never freed, pointer identity == path equality).
// Passing NULL or "" returns the empty-path sentinel (non-NULL stable pointer).
const char* aiml_path_intern(const char* raw);

// ── Composition ───────────────────────────────────────────────────────────────
// Join two path segments, normalize, intern.  POSIX semantics: if rhs is
// absolute, lhs is discarded.
const char* aiml_path_join(const char* lhs, const char* rhs);

// Join lhs with the variadic segment list terminated by a NULL sentinel.
// Convenience for join-chains; each segment may itself be a path or string.
// (Not variadic in C; accepts up to 4 extra segments via fixed args – the
//  compiler emits sequential aiml_path_join calls for more.)

// ── Decomposition ────────────────────────────────────────────────────────────
const char* aiml_path_parent(const char* p);       // parent directory (interned)
const char* aiml_path_name(const char* p);         // filename component (malloc'd)
const char* aiml_path_stem(const char* p);         // filename without extension (malloc'd)
const char* aiml_path_extension(const char* p);    // extension incl. '.' (malloc'd)
const char* aiml_path_root(const char* p);         // root component, e.g. "/" (interned)

// ── Properties ───────────────────────────────────────────────────────────────
int aiml_path_is_absolute(const char* p);          // returns 1/0
int aiml_path_is_relative(const char* p);
int aiml_path_is_empty(const char* p);
int aiml_path_depth(const char* p);                // number of components

// ── Relative path ─────────────────────────────────────────────────────────────
const char* aiml_path_relative_to(const char* p, const char* base); // intern'd

// ── Safe join (traversal guard) ───────────────────────────────────────────────
// Returns NULL if the joined result would escape base directory.
const char* aiml_path_safe_join(const char* base, const char* rhs);

// ── Conversion ───────────────────────────────────────────────────────────────
// to_string: identity (path IS the string); returns same pointer.
const char* aiml_path_to_string(const char* p);

// ── Platform directories ─────────────────────────────────────────────────────
const char* aiml_path_home(void);
const char* aiml_path_temp_dir(void);
const char* aiml_path_cwd(void);
const char* aiml_path_current_exe(void);
const char* aiml_path_config_dir(const char* app_name);  // ~/.config/<app>

// ── Filesystem operations (io namespace) ─────────────────────────────────────
int         aiml_io_exists(const char* p);         // 1 if path exists
int         aiml_io_is_dir(const char* p);
int         aiml_io_is_file(const char* p);
long long   aiml_io_file_size(const char* p);      // -1 on error
int         aiml_io_mkdir(const char* p);          // 0 = ok
int         aiml_io_mkdir_all(const char* p);      // mkdir -p
int         aiml_io_remove(const char* p);         // unlink/rmdir
int         aiml_io_remove_all(const char* p);     // rm -rf
int         aiml_io_rename(const char* src, const char* dst);
int         aiml_io_link(const char* src, const char* dst);   /* harter Link, kein Kopieren */
int         aiml_io_set_exec(const char* p, int on);          /* Ausführbar-Bit */
int         aiml_io_symlink(const char* target, const char* link_path);
int         aiml_io_is_symlink(const char* p);
char*       aiml_io_read_link(const char* p);                 /* owned rc-string, "" bei Fehler */
char*       aiml_io_read_file(const char* p);      // malloc'd content, caller frees
int         aiml_io_write_file(const char* p, const char* content); // 0=ok, -1=error
int         aiml_io_write_file_buf(const char* p, const void* data, int32_t len); // binary-safe, 0=ok, -1=error
// Fill a pre-allocated AimlList {i32 size, i32 capacity, void* data} with interned path entries.
// list_ptr must point to an already-initialized (malloc'd data) GenericList struct.
// Returns number of entries added, -1 on error.
int         aiml_io_list_dir_fill(const char* p, void* list_ptr);
int         aiml_io_can_read(const char* p);
int         aiml_io_can_write(const char* p);
int         aiml_io_can_exec(const char* p);

#ifdef __cplusplus
}
#endif
