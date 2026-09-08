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

// sys.env — build a map<string:string> of all process environment variables.
// Returns an AimlHttpMap* whose { size, capacity, occupied, keys, values }
// layout matches AIML's map<K:V>, so map methods (get/contains/…) work on it.
void* aiml_sys_env(void) {
    AimlHttpMap* m = aiml_http_map_new();
    if (!environ) return m;
    for (char** e = environ; *e != nullptr; ++e) {
        const char* entry = *e;
        const char* eq = strchr(entry, '=');
        if (!eq) continue;                       // skip malformed entries
        std::string key(entry, (size_t)(eq - entry));
        aiml_http_map_put(m, key.c_str(), eq + 1);
    }
    return m;
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
