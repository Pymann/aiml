#include "trace_runtime.h"
#include <cstdlib>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <string>
#include <unordered_set>

// ── TraceRuntime ──────────────────────────────────────────────────────────────
// Internal C++ class — one singleton, owns the active-function set.

class TraceRuntime {
    bool initialized_ = false;
    bool trace_all_   = false;
    std::unordered_set<std::string> active_;

public:
    void initFromString(const std::string& val) {
        if (val == "*" || val == "all") {
            trace_all_ = true;
            return;
        }
        // comma-separated list of function names (spaces stripped)
        size_t pos = 0;
        while (pos <= val.size()) {
            size_t comma = val.find(',', pos);
            if (comma == std::string::npos) comma = val.size();
            std::string tok = val.substr(pos, comma - pos);
            // strip leading/trailing spaces
            size_t s = tok.find_first_not_of(" \t");
            size_t e = tok.find_last_not_of(" \t");
            if (s != std::string::npos)
                active_.insert(tok.substr(s, e - s + 1));
            pos = comma + 1;
        }
    }

    void init() {
        if (initialized_) return;
        initialized_ = true;
        const char* env = getenv("AIML_TRACE");
        if (!env || !*env) return;
        initFromString(std::string(env));
    }

    void initBaked(const char* val) {
        if (initialized_) return;
        initialized_ = true;
        if (!val || !*val) return;
        initFromString(std::string(val));
    }

    bool is_active(const char* fname) const {
        if (!initialized_ || (!trace_all_ && active_.empty())) return false;
        if (trace_all_) return true;
        return active_.count(std::string(fname)) > 0;
    }

    void log(const char* fname, const char* dir, const char* args) const {
        fprintf(stderr, "[TRACE] %s %s(%s)\n", dir, fname, args ? args : "");
    }
};

static TraceRuntime g_trace;

extern "C" {

void aiml_trace_init() {
    g_trace.init();
}

void aiml_trace_init_baked(const char* val) {
    g_trace.initBaked(val);
}

int aiml_trace_active(const char* fname) {
    return g_trace.is_active(fname) ? 1 : 0;
}

void aiml_trace(const char* fname, const char* dir, const char* args) {
    g_trace.log(fname, dir, args);
}

void aiml_trace_format(const char* fname, const char* format, ...) {
    fprintf(stderr, "[TRACE] => %s(", fname ? fname : "?");
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format ? format : "", args);
    va_end(args);
    fputs(")\n", stderr);
}

// ── AIML_SIZETRACE emit ───────────────────────────────────────────────────────
// Compile-time gated: the compiler only emits calls to this for instrumented
// functions, so this just formats one line.
void aiml_sizetrace(const char* fname, const char* sym, const char* type,
                    long long size, const void* addr) {
    fprintf(stderr, "[SIZETRACE] %s | %-18s : %-22s size=%-6lld addr=%p\n",
            fname ? fname : "?", sym ? sym : "?", type ? type : "?", size, addr);
}

} // extern "C"
