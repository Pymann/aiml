// spec/23 §4.1 — Host-Parallelitaet: drei Fragen, drei Funktionen.
#pragma once
#include <cstdint>

extern "C" {
    // sys.core_count() -> i32: physische Kerne (ohne SMT-Geschwister).  Nie < 1.
    int32_t aiml_sys_core_count(void);

    // sys.logical_thread_count() -> i32: logische Prozessoren, die dieser Prozess
    // benutzen DARF (Affinitaets-Maske → taskset/cpuset wirken).  Nie < 1.
    int32_t aiml_sys_logical_thread_count(void);

    // sys.quota_thread_count() -> i32: CPU-Budget aus dem cgroup-Quota in ganzen
    // Threads (abgerundet).  Ohne Quota == logical_thread_count.  Nie < 1.
    int32_t aiml_sys_quota_thread_count(void);
}
