#pragma once

// AIML_TRACE runtime — env-var controlled function tracing.
//
// Usage:
//   AIML_TRACE=*              trace all user-defined functions
//   AIML_TRACE=foo,bar,baz    trace specific functions by name
//
// Output goes to stderr:
//   [TRACE] => foo(x=3, y=7)

extern "C" {
    // Called once at JIT startup to parse AIML_TRACE env var.
    void aiml_trace_init();

    // Called from AOT-compiled main() with the compile-time AIML_TRACE value baked in.
    // Initializes from the given string instead of getenv(). No-op if already initialized.
    void aiml_trace_init_baked(const char* val);

    // Returns 1 if the given function should be traced, 0 otherwise.
    // Fast O(1) hash lookup after init.
    int  aiml_trace_active(const char* fname);

    // Emit one trace line to stderr: "[TRACE] dir fname(args)"
    void aiml_trace(const char* fname, const char* dir, const char* args);
}
