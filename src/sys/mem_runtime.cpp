#include "mem_runtime.h"
#include "http_runtime.h"  // AimlHttpMap + aiml_http_map_new/put (map<K:V> layout shim)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#ifdef __linux__
#include <sys/resource.h>  // setrlimit, getrlimit
#include <unistd.h>        // getpagesize
#endif

#ifdef __EMSCRIPTEN__
#include <malloc.h>        // mallinfo — used bytes in the WASM/dlmalloc heap
#endif

extern char **environ;

extern "C" {

// spec/61 §5 (memV3): a map<string:string> is a refcounted HANDLE [i32 rc][i32 pad][AimlMapRaw],
// with a DENSE store (keys[0..size)/vals[0..size)) of aiml-managed strings, freed by
// aiml_map_handle_release → aiml_map_free (aiml_str_decref per entry). Declared here (defined in
// string_runtime.cpp) so sys.env produces the SAME representation the caller's scope-drop expects.
extern "C" void* aiml_map_handle_new(void);
extern "C" char* aiml_str_dup(const char* s);

// sys.env — build a map<string:string> of all process environment variables.
// Returns a memV3 map HANDLE (rc=1) with a dense store of aiml-managed string keys/values, so the
// caller's scope-drop (aiml_map_handle_release) frees it correctly. The old AimlHttpMap* return had
// NO [rc][pad] header and used libc-strdup'd (header-less) strings in an open-addressing layout →
// release read the rc header 8 bytes out of bounds and decref'd header-less strings → heap
// corruption ("corrupted size vs. prev_size" at exit, test_system).
void* aiml_sys_env(void) {
    void* handle = aiml_map_handle_new();       // returns a pointer at the AimlMapRaw part
    if (!handle) return handle;
    AimlHttpMap* m = static_cast<AimlHttpMap*>(handle);  // AimlHttpMap == AimlMapRaw layout
    if (!environ) return handle;
    int32_t n = 0;
    for (char** e = environ; *e != nullptr; ++e) ++n;
    if (n == 0) return handle;
    m->keys     = static_cast<char**>(calloc((size_t)n, sizeof(char*)));
    m->values   = static_cast<char**>(calloc((size_t)n, sizeof(char*)));
    m->occupied = nullptr;                       // dense store; occ bitmap is vestigial
    m->capacity = n;
    if (!m->keys || !m->values) { m->size = 0; return handle; }
    int32_t idx = 0;
    for (char** e = environ; *e != nullptr; ++e) {
        const char* entry = *e;
        const char* eq = strchr(entry, '=');
        if (!eq) continue;                       // skip malformed entries
        std::string key(entry, (size_t)(eq - entry));
        m->keys[idx]   = aiml_str_dup(key.c_str());
        m->values[idx] = aiml_str_dup(eq + 1);
        ++idx;
    }
    m->size = idx;
    return handle;
}

void aiml_sys_mem_limit(int64_t bytes) {
#ifdef __linux__
    struct rlimit rl;
    if (bytes <= 0) {
        // Remove limit — restore to original hard limit
        struct rlimit cur;
        getrlimit(RLIMIT_AS, &cur);
        rl.rlim_cur = cur.rlim_max;
        rl.rlim_max = cur.rlim_max;
    } else {
        // Set soft limit only — keeps hard limit unchanged so we can restore later
        struct rlimit cur;
        getrlimit(RLIMIT_AS, &cur);
        rl.rlim_cur = (rlim_t)bytes;
        rl.rlim_max = cur.rlim_max;  // preserve original hard limit
    }
    if (setrlimit(RLIMIT_AS, &rl) != 0) {
        fprintf(stderr, "[AIML] Warning: sys.mem_limit failed to set limit\n");
    } else if (bytes > 0) {
        fprintf(stderr, "[AIML] Memory limit set: %ld MB\n",
                (long)(bytes / (1024 * 1024)));
    }
#else
    (void)bytes;
    fprintf(stderr, "[AIML] Warning: sys.mem_limit not supported on this platform\n");
#endif
}

int64_t aiml_sys_mem_usage(void) {
#ifdef __linux__
    // Read RSS from /proc/self/statm (field 1, in pages)
    FILE* f = fopen("/proc/self/statm", "r");
    if (!f) return 0;
    long pages_total = 0, pages_rss = 0;
    if (fscanf(f, "%ld %ld", &pages_total, &pages_rss) < 2) {
        fclose(f);
        return 0;
    }
    fclose(f);
    return (int64_t)pages_rss * (int64_t)getpagesize();
#elif defined(__EMSCRIPTEN__)
    // WASM: es gibt kein /proc.  mallinfo().uordblks = aktuell belegte Bytes im
    // dlmalloc-Heap → reicht, um client-seitig Wachstum/Explosion zu beobachten.
    struct mallinfo mi = mallinfo();
    return (int64_t)(uint32_t)mi.uordblks;
#else
    return 0;
#endif
}

} // extern "C"
