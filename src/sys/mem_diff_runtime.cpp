// AIML_MEM_DIFF — a runtime live-allocation diff, keyed by call-stack (spec/48).
//
// GOAL: "what is accumulating?" — without a 20-line manual DECL/DROP tally or an 866 MB trace.
// Every AIML heap leaf-allocation (a string via aiml_str_alloc, a list/map data buffer) is recorded
// in a live map keyed by the C++ call-stack at the allocation; the matching free forgets it. Because
// tracking is at the LEAF level and deep-free (aiml_ser_free_decoded) recurses to those same leaves,
// a recursive model / nested class-member / list<Model>-of-lists is captured and reclaimed cleanly
// with no per-type logic: each nested string/buffer is one balanced record/forget.
//
// aiml_memdiff_dump() prints, per allocation site, the LIVE bytes+count AND the DELTA since the last
// dump — so a site that keeps growing between two dumps is the leak, in the function that made it
// (symbolized via backtrace_symbols; on the AOT game with debug info that reads as e.g.
// `CardService_get_base`). Call it every ~30 s from the [mem] tick via `sys.mem_diff()`.
//
// Gating is RUNTIME (env AIML_MEM_DIFF truthy), so it needs NO recompile of the app — just rebuild
// libaiml and launch with AIML_MEM_DIFF=1. Off = one cached bool test per alloc/free (negligible).
//
// Thread-safe (one mutex). Recursion into the tracker's own std allocations is prevented by a
// thread-local guard (those go through global new/malloc, never aiml_str_alloc, but the guard also
// covers backtrace()'s first-call internal allocation).

#include "mem_diff_runtime.h"
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <algorithm>

#include <string>
#ifndef __EMSCRIPTEN__
#include <execinfo.h>
#include <thread>
#include <chrono>
#ifndef __EMSCRIPTEN__
#include <malloc.h>     // mallinfo2: distinguish a real malloc leak (uordblks grows) from arena
#include <unistd.h>     // fragmentation (uordblks flat, RSS grows). Native only.
#endif
#else
#include <emscripten.h>
#endif

namespace {

constexpr int kCaptureFrames = 28;   // frames grabbed per allocation (deep ser_decode nesting eats ~8)
constexpr int kSkipFrames    = 2;    // drop aiml_memdiff_record + the leaf allocator frame
constexpr int kPrintFrames   = 22;   // frames shown per site in the dump
constexpr int kTopSites      = 30;   // sites listed per dump

struct LiveBlock { size_t size; uint64_t site; };
struct Site {
    void*  frames[kCaptureFrames];
    int    n_frames = 0;
#ifdef __EMSCRIPTEN__
    char   stack_text[1024] = {0};   // WASM has no backtrace() → a textual callstack per site instead
#endif
    size_t live_count = 0;
    size_t live_bytes = 0;
    size_t total_count = 0;      // ever recorded (for churn context)
    size_t last_dump_bytes = 0;  // live_bytes at the previous dump → delta
};

std::mutex g_mtx;
std::unordered_map<const void*, LiveBlock> g_live;   // data ptr → {size, site}
std::unordered_map<uint64_t, Site>         g_sites;  // site hash → aggregate
size_t g_total_live_bytes = 0;
size_t g_total_live_count = 0;
bool   g_dump_registered = false;

// thread-local recursion guard (records happen on any thread that allocates AIML strings).
thread_local bool t_inside = false;

// Baked activation state (set once by aiml_memdiff_init_baked; NO getenv at runtime). g_on is the
// single gate the leaf-hooks branch on — false unless the compiler emitted the init call, i.e. unless
// AIML_MEM_DIFF was set at compile time.
bool        g_on = false;
const char* g_file = nullptr;   // AIML_MEM_DIFF_FILE (baked); the target filename / POST filename field
const char* g_endpoint = nullptr; // AIML_MEM_DIFF_ENDPOINT (baked); HTTP receiver URL, else local file
long        g_interval = 60;    // AIML_MEM_DIFF_INTERVAL seconds (baked); 0 → no periodic dump
time_t      g_last_dump = 0;
unsigned    g_record_ticks = 0; // opportunistic periodic-check counter

inline int  enabled_cached() { return g_on ? 1 : 0; }
inline const char* out_file_cached() { return g_file; }
inline long interval_cached() { return g_interval; }

uint64_t hash_frames(void* const* fr, int n) {
    uint64_t h = 1469598103934665603ull;         // FNV-1a
    for (int i = 0; i < n; ++i) {
        uint64_t p = reinterpret_cast<uint64_t>(fr[i]);
        h ^= p; h *= 1099511628211ull;
    }
    return h;
}

} // namespace

extern "C" void aiml_memdiff_init_baked(const char* file, int32_t interval_seconds, const char* endpoint) {
    if (g_on) return;                                // once
    g_on = true;
    if (file && *file)         { g_file = strdup(file); }         // leaked once, lives for the process
    if (endpoint && *endpoint) { g_endpoint = strdup(endpoint); }
    g_interval = (interval_seconds < 0) ? 0 : (long)interval_seconds;
#ifndef __EMSCRIPTEN__
    // Native: a real background timer thread → reliable interval dumps INDEPENDENT of allocation
    // volume (an idle server that allocates < a few thousand strings/min would otherwise never trip
    // the activity-driven check, and a `kill -9` skips atexit). WASM has no threads → the
    // activity-driven periodic in record() covers the (allocation-heavy) client.
    if (g_interval > 0) {
        long iv = g_interval;
        std::thread([iv]() {
            for (;;) {
                std::this_thread::sleep_for(std::chrono::seconds(iv));
                aiml_memdiff_dump();
            }
        }).detach();
    }
#endif
}

extern "C" int aiml_memdiff_enabled(void) { return enabled_cached(); }

extern "C" void aiml_memdiff_record(void* ptr, size_t size) {
    if (!enabled_cached() || !ptr || t_inside) return;
    t_inside = true;
    // Register a process-exit dump on the first allocation, so a leak snapshot prints at shutdown
    // even if the app never calls sys.mem_diff() — AIML_MEM_DIFF=1 in the runtime env is enough.
    if (!g_dump_registered) {
        g_dump_registered = true;
        atexit([](){ aiml_memdiff_dump(); });
    }
    void* bt[kCaptureFrames + kSkipFrames];
    int n = 0;
    uint64_t site;
#ifndef __EMSCRIPTEN__
    n = backtrace(bt, kCaptureFrames + kSkipFrames);
    int start = (n > kSkipFrames) ? kSkipFrames : 0;
    int cnt = n - start; if (cnt > kCaptureFrames) cnt = kCaptureFrames; if (cnt < 0) cnt = 0;
    site = hash_frames(bt + start, cnt);
#else
    // WASM: no execinfo backtrace → capture a TEXTUAL callstack (emscripten builds it from a JS
    // Error stack; with the client's -g2 the frames carry function names). The site key is the FNV
    // hash of that text, so identical stacks aggregate into one site — real per-callstack attribution.
    static thread_local char stkbuf[1024];
    stkbuf[0] = 0;
    emscripten_get_callstack(EM_LOG_C_STACK | EM_LOG_NO_PATHS, stkbuf, (int)sizeof(stkbuf));
    uint64_t h = 1469598103934665603ull;                      // FNV-1a over the stack text
    for (const char* p = stkbuf; *p; ++p) { h ^= (unsigned char)*p; h *= 1099511628211ull; }
    site = h;
#endif
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        auto& s = g_sites[site];
#ifndef __EMSCRIPTEN__
        if (s.n_frames == 0 && cnt > 0) {
            s.n_frames = cnt;
            for (int i = 0; i < cnt; ++i) s.frames[i] = bt[start + i];
        }
#else
        if (s.stack_text[0] == 0 && stkbuf[0] != 0) {
            strncpy(s.stack_text, stkbuf, sizeof(s.stack_text) - 1);
            s.stack_text[sizeof(s.stack_text) - 1] = 0;
        }
#endif
        s.live_count++; s.live_bytes += size; s.total_count++;
        g_live[ptr] = LiveBlock{ size, site };
        g_total_live_bytes += size; g_total_live_count++;
    }
#ifdef __EMSCRIPTEN__
    // WASM has no background thread → activity-driven periodic: every 2048th record, if the interval
    // elapsed, dump. The client allocates plenty during UI, so this trips reliably. (Native uses the
    // real timer thread started in init_baked instead — see there.)
    long iv = interval_cached();
    if (iv > 0 && (++g_record_ticks & 0x7FF) == 0) {
        time_t now = time(nullptr);
        if (g_last_dump == 0) g_last_dump = now;              // arm on first record
        else if (now - g_last_dump >= iv) { t_inside = false; aiml_memdiff_dump(); return; }
    }
#endif
    t_inside = false;
}

extern "C" void aiml_memdiff_forget(void* ptr) {
    if (!enabled_cached() || !ptr || t_inside) return;
    t_inside = true;
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        auto it = g_live.find(ptr);
        if (it != g_live.end()) {
            auto sit = g_sites.find(it->second.site);
            if (sit != g_sites.end()) {
                if (sit->second.live_count) sit->second.live_count--;
                if (sit->second.live_bytes >= it->second.size) sit->second.live_bytes -= it->second.size;
                else sit->second.live_bytes = 0;
            }
            if (g_total_live_bytes >= it->second.size) g_total_live_bytes -= it->second.size;
            if (g_total_live_count) g_total_live_count--;
            g_live.erase(it);
        }
    }
    t_inside = false;
}

extern "C" long long aiml_memdiff_dump(void) {
    if (!enabled_cached()) return 0;
    std::vector<Site> snapshot;
    long long total;
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        snapshot.reserve(g_sites.size());
        for (auto& kv : g_sites)
            if (kv.second.live_bytes > 0 || kv.second.live_count > 0) snapshot.push_back(kv.second);
        total = (long long)g_total_live_bytes;
        // stamp last_dump_bytes for the NEXT delta (write back into the live map)
        for (auto& kv : g_sites) kv.second.last_dump_bytes = kv.second.live_bytes;
    }
    // Sort by delta-since-last-dump (what is actively growing), then by live bytes.
    std::sort(snapshot.begin(), snapshot.end(), [](const Site& a, const Site& b) {
        long long da = (long long)a.live_bytes - (long long)a.last_dump_bytes;
        long long db = (long long)b.live_bytes - (long long)b.last_dump_bytes;
        if (da != db) return da > db;
        return a.live_bytes > b.live_bytes;
    });
    // Build the whole dump into one string, then SINK it (below): to the HTTP endpoint if one was
    // baked (the browser/WASM client, which can't write a real disk file → the memlog host does), or
    // to the local file (native servers; or WASM MEMFS as a fallback), or stderr.
    char ts[32] = {0};
    time_t now = time(nullptr);
    struct tm tmv; localtime_r(&now, &tmv);
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tmv);
    std::string txt; txt.reserve(4096);
    char line[512];
    snprintf(line, sizeof(line), "[mem-diff %s] total live: %lld bytes in %zu blocks across %zu sites"
             " (top %d by growth since last dump)\n",
             ts, total, g_total_live_count, snapshot.size(), kTopSites);
    txt += line;
#ifndef __EMSCRIPTEN__
    {
        // Total malloc accounting vs RSS: if uordblks tracks RSS growth → a real (untracked-type)
        // malloc leak; if uordblks is flat while RSS climbs → glibc arena fragmentation, not a leak.
        struct mallinfo2 mi = mallinfo2();
        long rss_kb = 0;
        FILE* sf = fopen("/proc/self/statm", "r");
        if (sf) { long tot = 0, res = 0; if (fscanf(sf, "%ld %ld", &tot, &res) == 2) rss_kb = res * (sysconf(_SC_PAGESIZE) / 1024); fclose(sf); }
        snprintf(line, sizeof(line),
                 "  [malloc] uordblks(in-use)=%zu arena(brk)=%zu hblkhd(mmap)=%zu fordblks(free)=%zu | RSS=%ld KB\n",
                 (size_t)mi.uordblks, (size_t)mi.arena, (size_t)mi.hblkhd, (size_t)mi.fordblks, rss_kb);
        txt += line;
    }
#endif
    int shown = 0;
    for (const auto& s : snapshot) {
        if (shown++ >= kTopSites) break;
        long long delta = (long long)s.live_bytes - (long long)s.last_dump_bytes;
        snprintf(line, sizeof(line), "  [%+lld B]  live %zu B / %zu blocks (ever %zu)\n",
                 delta, s.live_bytes, s.live_count, s.total_count);
        txt += line;
#ifndef __EMSCRIPTEN__
        char** syms = backtrace_symbols(const_cast<void* const*>(s.frames), s.n_frames);
        if (syms) {
            for (int i = 0; i < s.n_frames && i < kPrintFrames; ++i) {
                txt += "        "; txt += syms[i]; txt += "\n";
            }
            free(syms);
        }
#else
        // Print the captured textual callstack, one frame per line (already newline-separated),
        // indented and capped to a handful of frames so the dump stays compact.
        {
            const char* p = s.stack_text;
            int shown_frames = 0;
            while (*p && shown_frames < 12) {
                const char* nl = strchr(p, '\n');
                size_t seg = nl ? (size_t)(nl - p) : strlen(p);
                txt += "        "; txt.append(p, seg); txt += "\n";
                shown_frames++;
                if (!nl) break;
                p = nl + 1;
            }
        }
#endif
    }

    if (g_endpoint) {
        // 2-field wire format: line 1 = filename, rest = content. Fire-and-forget POST (never blocks
        // or throttles the app; at ~1/min it cannot flood the receiver).
        std::string body;
        body += (g_file ? g_file : "mem_diff_client.txt");
        body += "\n";
        body += txt;
#ifdef __EMSCRIPTEN__
        EM_ASM({
            try {
                var ep = UTF8ToString($0);
                // A full URL is used verbatim; a host-relative endpoint (":8091/memdiff" or
                // "/memdiff") is resolved against the page's host, so it works on localhost AND when
                // the client is opened from a phone/LAN IP without rebaking the URL.
                var url = (ep.indexOf('http') === 0)
                    ? ep
                    : (location.protocol + '//' + location.hostname + ep);
                fetch(url, { method: 'POST', body: UTF8ToString($1),
                             headers: { 'Content-Type': 'text/plain' }, keepalive: true })
                    .catch(function(){});
            } catch (e) {}
        }, g_endpoint, body.c_str());
#else
        // Native with an endpoint configured but no emscripten fetch: fall back to the local file so
        // data is never silently lost. (Native servers normally configure a file, not an endpoint.)
        if (g_file) { FILE* f = fopen(g_file, "a"); if (f) { fwrite(txt.data(), 1, txt.size(), f); fclose(f); } }
#endif
    } else {
        const char* path = out_file_cached();
        FILE* out = path ? fopen(path, "a") : stderr;
        if (!out) out = stderr;
        fwrite(txt.data(), 1, txt.size(), out);
        fflush(out);
        if (out != stderr) fclose(out);
    }
    g_last_dump = now;
    return total;
}

extern "C" long long aiml_sys_mem_diff(void) {
    if (!g_dump_registered && enabled_cached()) {
        g_dump_registered = true;
        atexit([](){ aiml_memdiff_dump(); });   // final dump at process exit
    }
    return aiml_memdiff_dump();
}
