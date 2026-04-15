#include "mem_runtime.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef __linux__
#include <sys/resource.h>  // setrlimit, getrlimit
#include <unistd.h>        // getpagesize
#endif

extern "C" {

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
#else
    return 0;
#endif
}

} // extern "C"
