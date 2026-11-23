// spec/23 §4.1 — host parallelism: three questions, three functions.
#pragma once
#include <cstdint>

extern "C" {
    // sys.core_count() -> i32: physical cores (without SMT siblings).  Never < 1.
    int32_t aiml_sys_core_count(void);

    // sys.logical_thread_count() -> i32: logical processors this process
    // MAY use (affinity mask → taskset/cpuset take effect).  Never < 1.
    int32_t aiml_sys_logical_thread_count(void);

    // sys.quota_thread_count() -> i32: CPU budget from the cgroup quota in whole
    // threads (rounded down).  Without a quota == logical_thread_count.  Never < 1.
    int32_t aiml_sys_quota_thread_count(void);
}
