#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ZIM archives, read-only — spec/33_2 §6 (F33-2-20 … F33-2-34).
 *
 * An entry is a VALUE with no heap of its own: the archive plus an index is all it takes to
 * answer every question about it.  That keeps `list<zim_entry>` copyable and leak-free (a
 * scalar-only struct needs no release descriptor), which is what §6.2 means by "it holds only
 * an index into the open archive".  The strings are read on demand through the handle.
 *
 * Layout MUST match the zim_entry struct the compiler registers (builtin_registry.cpp):
 *   { i64 index, i64 size, i64 position, i8 found, i8 is_redirect }
 */
typedef struct {
    int64_t index;        /* entry index in path order, stable for a given file */
    int64_t size;         /* uncompressed content size; 0 for a redirect */
    int64_t position;     /* cluster-order position, -1 unless it came from scan */
    int8_t  found;        /* 0 = the lookup matched nothing; every other field is then 0 */
    int8_t  is_redirect;
} AimlZimEntry;

/* Which string to read with aiml_zim_str */
enum {
    AIML_ZIM_STR_PATH          = 0,
    AIML_ZIM_STR_TITLE         = 1,
    AIML_ZIM_STR_MIMETYPE      = 2,
    AIML_ZIM_STR_REDIRECT_PATH = 3
};

/* Error codes of spec/33_2 §6.4 (F33-2-32) */
enum {
    AIML_ZIM_OK            = 0,
    AIML_ZIM_E_OPEN        = 1,   /* cannot open */
    AIML_ZIM_E_FORMAT      = 2,   /* not a ZIM file or unsupported version */
    AIML_ZIM_E_CORRUPT     = 3,   /* corrupt or truncated */
    AIML_ZIM_E_LOOP        = 4,   /* redirect loop */
    AIML_ZIM_E_IS_REDIRECT = 5,   /* entry is a redirect (content) */
    AIML_ZIM_E_NO_ENTRY    = 6    /* no such entry */
};

/* The last error on THIS thread, for the ZimError the compiler throws.
 * aiml_zim_last_message returns a fresh aiml-managed string (spec/61 §4). */
int32_t aiml_zim_last_code(void);
char*   aiml_zim_last_message(void);

/* Opening and metadata (§6.1).  aiml_zim_open returns NULL and sets the error on failure. */
void*   aiml_zim_open(const char* path);
void    aiml_zim_close(void* handle);
int64_t aiml_zim_entry_count(void* handle);
int64_t aiml_zim_article_count(void* handle);
// §6.3: the number of positions the cluster order has (the user entries, without metadata).
int64_t aiml_zim_scan_count(void* handle);
char*   aiml_zim_uuid(void* handle);
char*   aiml_zim_metadata(void* handle, const char* key);
int32_t aiml_zim_checksum_ok(void* handle);

/* Entries (§6.2).  by_path/by_title never fail: a miss sets found = 0. */
void    aiml_zim_by_path(void* handle, const char* path, AimlZimEntry* out);
void    aiml_zim_by_title(void* handle, const char* title, AimlZimEntry* out);
int32_t aiml_zim_resolve(void* handle, const AimlZimEntry* in, AimlZimEntry* out);
char*   aiml_zim_str(void* handle, const AimlZimEntry* entry, int32_t which);
void*   aiml_zim_content(void* handle, const AimlZimEntry* entry);       /* AimlCompressBuffer* */
char*   aiml_zim_content_str(void* handle, const AimlZimEntry* entry);

/* Scanning in cluster order (§6.3).  Fills `list_ptr` (a list<zim_entry>) and returns the
 * number of entries appended; 0 at the end. */
int32_t aiml_zim_scan(void* handle, int64_t from, int32_t count, void* list_ptr);

/* Cluster cache (§6.3).  libzim 8 has no setter; it reads ZIM_CLUSTERCACHE (a NUMBER OF
 * CLUSTERS) when an archive is opened, so this takes effect for archives opened afterwards. */
void    aiml_zim_cache(int64_t clusters);

/* ── The loadable module (spec/33_2 §6.4, F33-2-34) ─────────────────────────────────────────
 *
 * Everything above is a thin forwarder in libaiml; the libzim code itself lives in a SEPARATE
 * shared object, libaiml_zim.so, that libaiml loads on first use.
 *
 * The reason is the link, not taste: libzim needs libxapian for its full-text search, which
 * AIML never calls.  Linking libzim into libaiml made libxapian a transitive dependency of
 * EVERY aiml program, and DT_RUNPATH is not searched for a dependency's own dependencies — so
 * every existing project suddenly failed to link with "undefined reference to Xapian::...",
 * ZIM or no ZIM (measured 2026-11-03 on tests/import_norm, bundle_socket and venv).  Keeping
 * the code in a module that is opened on demand puts that cost only on programs that read a
 * ZIM file, which is what "optional" has to mean.
 *
 * The module exports exactly one symbol: this table.  Entry points are matched by POSITION,
 * so a field is only ever appended, never moved. */
typedef struct {
    int32_t version;                 /* 1 */
    void*   (*open)(const char*);
    void    (*close)(void*);
    int64_t (*entry_count)(void*);
    int64_t (*article_count)(void*);
    int64_t (*scan_count)(void*);
    char*   (*uuid)(void*);
    char*   (*metadata)(void*, const char*);
    int32_t (*checksum_ok)(void*);
    void    (*by_path)(void*, const char*, AimlZimEntry*);
    void    (*by_title)(void*, const char*, AimlZimEntry*);
    int32_t (*resolve)(void*, const AimlZimEntry*, AimlZimEntry*);
    char*   (*str)(void*, const AimlZimEntry*, int32_t);
    void*   (*content)(void*, const AimlZimEntry*);
    char*   (*content_str)(void*, const AimlZimEntry*);
    int32_t (*scan)(void*, int64_t, int32_t, void*);
    void    (*cache)(int64_t);
    int32_t (*last_code)(void);
    char*   (*last_message)(void);
} AimlZimApi;

#define AIML_ZIM_API_VERSION 1

/* Exported by libaiml_zim.so only. */
const AimlZimApi* aiml_zim_module_api(void);

#ifdef __cplusplus
}
#endif
