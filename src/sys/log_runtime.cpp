/* ── log_runtime.cpp — Spec 46: leveled logging ──────────────────────────── */

#include "log_runtime.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <atomic>
#include <mutex>
#include <unistd.h>   /* isatty */

/* ── Internal state ──────────────────────────────────────────────────────── */

enum LogLevel : int {
    LVL_VERBOSE = 0,
    LVL_DEBUG   = 1,
    LVL_INFO    = 2,
    LVL_WARN    = 3,
    LVL_ERROR   = 4,
    LVL_FATAL   = 5,
};

static std::atomic<int> g_threshold{LVL_INFO};
static FILE*            g_log_file   = nullptr;
static std::mutex       g_log_mutex;
static std::atomic<int> g_stderr_tty{-1};  /* -1 = uninitialized */

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static bool stderr_is_tty() {
    if (g_stderr_tty < 0) g_stderr_tty = isatty(STDERR_FILENO) ? 1 : 0;
    return g_stderr_tty == 1;
}

static const char* level_tag(int lvl) {
    switch (lvl) {
        case LVL_VERBOSE: return "VERBOSE";
        case LVL_DEBUG:   return "DEBUG";
        case LVL_INFO:    return "INFO";
        case LVL_WARN:    return "WARN";
        case LVL_ERROR:   return "ERROR";
        case LVL_FATAL:   return "FATAL";
        case 100:         return "DEPRECATED";
        case 101:         return "SYSTEM";
        default:          return "???";
    }
}

/* ANSI color codes */
static const char* level_color(int lvl) {
    switch (lvl) {
        case LVL_VERBOSE: return "\033[90m";        /* dark gray  */
        case LVL_DEBUG:   return "\033[36m";        /* cyan       */
        case LVL_INFO:    return "\033[32m";        /* green      */
        case LVL_WARN:    return "\033[33m";        /* yellow     */
        case LVL_ERROR:   return "\033[31m";        /* red        */
        case LVL_FATAL:   return "\033[1;31m";      /* red bold   */
        case 100:         return "\033[35m";        /* magenta    */
        case 101:         return "\033[34m";        /* blue       */
        default:          return "";
    }
}

static void emit(int lvl, const char* msg, const char* loc) {
    /* Timestamp */
    time_t now = time(nullptr);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    char ts[20];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_buf);

    const char* tag = level_tag(lvl);

    std::lock_guard<std::mutex> lock(g_log_mutex);

    /* stderr — with optional color */
    if (stderr_is_tty()) {
        const char* c = level_color(lvl);
        fprintf(stderr, "%s %s[%-10s]\033[0m %s  (%s)\n", ts, c, tag, msg ? msg : "", loc ? loc : "?");
    } else {
        fprintf(stderr, "%s [%-10s] %s  (%s)\n", ts, tag, msg ? msg : "", loc ? loc : "?");
    }

    /* file mirror — never colored */
    if (g_log_file) {
        fprintf(g_log_file, "%s [%-10s] %s  (%s)\n", ts, tag, msg ? msg : "", loc ? loc : "?");
        fflush(g_log_file);
    }
}

/* ── Filterable levels ───────────────────────────────────────────────────── */

static inline void log_filtered(int lvl, const char* msg, const char* loc) {
    if (lvl >= g_threshold) emit(lvl, msg, loc);
}

extern "C" void aiml_log_verbose(const char* msg, const char* loc)    { log_filtered(LVL_VERBOSE, msg, loc); }
extern "C" void aiml_log_debug(const char* msg, const char* loc)      { log_filtered(LVL_DEBUG,   msg, loc); }
extern "C" void aiml_log_info(const char* msg, const char* loc)       { log_filtered(LVL_INFO,    msg, loc); }
extern "C" void aiml_log_warn(const char* msg, const char* loc)       { log_filtered(LVL_WARN,    msg, loc); }
extern "C" void aiml_log_error(const char* msg, const char* loc)      { log_filtered(LVL_ERROR,   msg, loc); }

extern "C" void aiml_log_fatal(const char* msg, const char* loc) {
    emit(LVL_FATAL, msg, loc);
    exit(1);
}

/* ── Always-visible levels ───────────────────────────────────────────────── */

extern "C" void aiml_log_deprecated(const char* msg, const char* loc) { emit(100, msg, loc); }
extern "C" void aiml_log_system(const char* msg, const char* loc)     { emit(101, msg, loc); }

/* ── Configuration ───────────────────────────────────────────────────────── */

extern "C" void aiml_log_set_level(const char* level) {
    if (!level) return;
    if      (strcmp(level, "verbose") == 0) g_threshold = LVL_VERBOSE;
    else if (strcmp(level, "debug")   == 0) g_threshold = LVL_DEBUG;
    else if (strcmp(level, "info")    == 0) g_threshold = LVL_INFO;
    else if (strcmp(level, "warn")    == 0) g_threshold = LVL_WARN;
    else if (strcmp(level, "error")   == 0) g_threshold = LVL_ERROR;
    else if (strcmp(level, "fatal")   == 0) g_threshold = LVL_FATAL;
}

extern "C" void aiml_log_set_file(const char* path) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (g_log_file) { fclose(g_log_file); g_log_file = nullptr; }
    if (path && path[0] != '\0') {
        g_log_file = fopen(path, "a");
    }
}

/* ── Diagnostic trace ring (AIML_MODEL_TRACE) ─────────────────────────────── */

namespace {

struct TraceEvent {
    const void*   ptr;      /* subject object                                  */
    const char*   op;       /* static/literal op name                          */
    unsigned long a;        /* op-specific (e.g. magic)                         */
    unsigned long b;        /* op-specific (e.g. refcount)                      */
    unsigned long seq;      /* monotonic order                                 */
    char          note[24]; /* short inline note (copied)                      */
};

constexpr int TRACE_RING = 1024;

std::mutex               g_trace_mutex;
TraceEvent               g_trace_ring[TRACE_RING];
unsigned long            g_trace_next = 0;     /* write cursor (mod TRACE_RING) */
std::atomic<unsigned long> g_trace_seq{0};

int compute_trace_level() {
    const char* v = getenv("AIML_MODEL_TRACE");
    if (!v || !v[0]) return 0;
    int n = atoi(v);
    return n > 0 ? n : (strcmp(v, "0") == 0 ? 0 : 1);
}

} // namespace

extern "C" int aiml_trace_level(void) {
    static int cached = compute_trace_level();   /* thread-safe init (C++11)   */
    return cached;
}

extern "C" void aiml_trace_record(const char* op, const void* ptr,
                                  unsigned long a, unsigned long b, const char* note) {
    if (!aiml_trace_level()) return;
    std::lock_guard<std::mutex> lock(g_trace_mutex);
    TraceEvent& e = g_trace_ring[g_trace_next % TRACE_RING];
    e.ptr = ptr;
    e.op  = op ? op : "?";
    e.a   = a;
    e.b   = b;
    e.seq = ++g_trace_seq;
    if (note) { strncpy(e.note, note, sizeof(e.note) - 1); e.note[sizeof(e.note) - 1] = '\0'; }
    else      { e.note[0] = '\0'; }
    ++g_trace_next;
}

extern "C" void aiml_trace_dump(const void* ptr, const char* reason) {
    if (!aiml_trace_level()) return;
    std::lock_guard<std::mutex> lock(g_trace_mutex);
    fprintf(stderr, "\n==== AIML_MODEL_TRACE dump: %s  (ptr=%p) ====\n",
            reason ? reason : "?", ptr);
    if (g_log_file)
        fprintf(g_log_file, "\n==== AIML_MODEL_TRACE dump: %s  (ptr=%p) ====\n",
                reason ? reason : "?", ptr);
    unsigned long total = g_trace_next < (unsigned long)TRACE_RING ? g_trace_next : (unsigned long)TRACE_RING;
    unsigned long start = g_trace_next >= (unsigned long)TRACE_RING ? g_trace_next - TRACE_RING : 0;
    for (unsigned long i = 0; i < total; ++i) {
        const TraceEvent& e = g_trace_ring[(start + i) % TRACE_RING];
        if (ptr && e.ptr != ptr) continue;
        fprintf(stderr, "  #%lu  %-14s ptr=%p a=0x%lx b=%lu %s\n",
                e.seq, e.op, e.ptr, e.a, e.b, e.note);
        if (g_log_file)
            fprintf(g_log_file, "  #%lu  %-14s ptr=%p a=0x%lx b=%lu %s\n",
                    e.seq, e.op, e.ptr, e.a, e.b, e.note);
    }
    fprintf(stderr, "==== end dump ====\n\n");
    if (g_log_file) { fprintf(g_log_file, "==== end dump ====\n\n"); fflush(g_log_file); }
}
