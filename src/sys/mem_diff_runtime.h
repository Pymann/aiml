// AIML_MEM_DIFF — live-allocation diff, keyed by call-stack. See mem_diff_runtime.cpp / spec/48.
#pragma once
#include <cstddef>
#include <cstdint>

extern "C" {
// Compiler-baked activation (AIML_TRACE model): the compiler emits ONE call to this in the program
// prologue iff AIML_MEM_DIFF was set at COMPILE time (read from exodus.json env). It turns tracking on
// and bakes the output file + dump interval — no runtime getenv, no defines. When not emitted, tracking
// stays off and the leaf-hooks are a single never-taken branch. file == nullptr → dump to stderr.
// file      = the target log filename (also the `filename` field when POSTing to an endpoint).
// interval  = seconds between automatic dumps (int32_t: fixed ABI on native + wasm32).
// endpoint  = optional HTTP receiver URL (e.g. the memlog host). When set, a dump is POSTed there
//             as two fields (line 1 = filename, rest = content) instead of written to a local file
//             — the path for the browser/WASM client, which cannot write a real disk file. nullptr
//             → write `file` locally (fopen; native servers, or WASM MEMFS).
void aiml_memdiff_init_baked(const char* file, int32_t interval_seconds, const char* endpoint);
// True once tracking has been activated by aiml_memdiff_init_baked.
int  aiml_memdiff_enabled(void);
// Record a freshly allocated block (data pointer + byte size) with its call-stack.
void aiml_memdiff_record(void* ptr, size_t size);
// Forget a block about to be freed (decrements its call-stack's live totals).
void aiml_memdiff_forget(void* ptr);
// Print the live-allocation diff (top sites by growth since the last dump) to stderr;
// returns total live bytes. Safe to call when disabled (returns 0, prints nothing).
long long aiml_memdiff_dump(void);
// sys.mem_diff() builtin entry point (dump + return total live bytes).
long long aiml_sys_mem_diff(void);
}
