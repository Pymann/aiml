#pragma once
#include <cstdint>

extern "C" {
    // sys.mem_limit(bytes) — set virtual memory limit for the process.
    // When exceeded, malloc returns NULL and the process is terminated
    // with a diagnostic message instead of freezing the machine.
    // Pass 0 to remove the limit.
    void aiml_sys_mem_limit(int64_t bytes);

    // sys.mem_usage() → i64 — current RSS (resident set size) in bytes.
    int64_t aiml_sys_mem_usage(void);
}
