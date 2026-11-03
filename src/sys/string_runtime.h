#pragma once
#include <cstdint>
#include <cstddef>  // size_t

extern "C" {

// ── Refcounted string allocator ──────────────────────────────────────────────
// All runtime-allocated strings should go through aiml_str_alloc/dup so that
// aiml_str_incref/decref can manage their lifetime.  Literals (rodata) work
// transparently — incref/decref no-op on them via a magic-header check.
//
// `len` is size_t to match `malloc(size_t)` and `strlen(...)` return types
// for clean drop-in replacement in `__aiml_str_concat` & friends (which use
// ctx.getSizeType() — i64 on x86_64, i32 on wasm32).  Internal storage is
// uint32 refcount, so practical max string is 4GB which we'll never hit.
char* aiml_str_alloc(size_t len);
// Immortal refcounted "" (spec/61): default for unset model string fields — safe to decref at
// drops-on free (no-op) while `.len`/strlen see an empty string. See string_runtime.cpp.
char* aiml_str_empty(void);
// Instrumented strcmp used by AIML string ==/!= : delegates to strcmp but logs
// (to stderr) when either operand is a corrupt/dangling pointer (bad magic AND
// unterminated within 256 bytes) — pinpoints bad comparisons right before an OOB.
int   aiml_dbg_strcmp(const char* a, const char* b);
char* aiml_str_dup(const char* s);
char* aiml_str_ndup(const char* s, size_t len);
// Appends an owned copy of s[0..len) to a list<string> (the {size,cap,data} list a
// runtime fill function receives).  0 = ok, -1 = out of memory.
int32_t aiml_list_push_str_copy(void* list_ptr, const char* s, size_t len);
void  aiml_str_incref(char* s);
void  aiml_str_decref(char* s);

// ── spec/61 §7 managed string-local assignment helpers ───────────────────────
// A "managed" local string slot holds EITHER nullptr OR exactly one owned RC reference.
// These maintain that invariant; they must only be applied to normalized managed slots
// (not to raw/borrowed member slots). They never read a header before a foreign address:
// dup reads only the source CONTENT; the old value is decref'd via the slot invariant, not
// via magic-guessing on an arbitrary pointer.
//
// COPY: acquire an independent reference to `src` (dup) BEFORE releasing the old slot value.
// `src` must be a live, NUL-terminated borrowed string, or null. Returns 0 on allocation
// failure (leaving *dst unchanged, old value intact); 1 on success.
int32_t aiml_str_assign_copy(char** dst, const char* src);
// MOVE: `next` transfers a separate owned RC reference (or null) into the slot; the old slot
// value is released. The caller must exclude a no-transfer self-assignment.
void    aiml_str_assign_move(char** dst, char* next);
// SHARE (spec/61 §4, AIML_MEM_V3): incref `src` (share its buffer) and release the old slot
// value — secure-before-release, so self-assignment `set:[x,x]` is a safe no-op. `src` may be a
// managed buffer, a literal, or the immortal "" (incref no-ops on the latter two). Immutable
// strings make sharing observationally identical to copying (no dup).
int32_t aiml_str_assign_share(char** dst, char* src);
// TAKE: read the slot value and null the slot (ownership handed to the caller). Used at a
// committed move-out point so the slot's scope-drop / next decref-old is a null no-op.
char*   aiml_str_take_local(char** src);

// string.lower() -> string — all ASCII uppercase to lowercase
char* aiml_str_lower(const char* s);

// string.upper() -> string — all ASCII lowercase to uppercase
char* aiml_str_upper(const char* s);

// string.title() -> string — capitalize first char of every word, lowercase
// the rest.  Word separators: whitespace, `-`, and `_`.  Useful for turning
// snake_case ids into display labels: "smith_touch" -> "Smith Touch".
char* aiml_str_title(const char* s);

// string.trim() -> string — strip leading+trailing whitespace
char* aiml_str_trim(const char* s);

// string.replace(old, new) -> string — replace all occurrences
char* aiml_str_replace(const char* s, const char* old_str, const char* new_str);

// string.split(delimiter) -> list<string>  (fills AimlListRaw via out-pointer)
// Returns number of parts written.
int aiml_str_split_fill(const char* s, const char* delim, void* list_ptr);

// string.parse_i32() -> i32 — like atoi but returns 0 for null/empty
int32_t aiml_str_parse_i32(const char* s);

// string.parse_f32() -> f32 — like strtof but returns 0.0 for null/empty
float aiml_str_parse_f32(const char* s);
double aiml_str_parse_f64(const char* s);
char* aiml_sys_format_fixed(double value, int32_t decimals);

}
