// spec/23 §4.1 — Host-Parallelitaet.  Drei Fragen, drei Funktionen:
//   core_count            physische Kerne           → CPU-gebundene Pools
//   logical_thread_count  Affinitaets-Maske         → was gleichzeitig laufen kann
//   quota_thread_count    cgroup cpu.max            → was der Container erlaubt
// Alle Werte werden EINMAL pro Prozess gelesen und gecacht (spec: deterministisch
// innerhalb eines Prozesses, Kategorie 2 = aus jedem Thread aufrufbar).  Ein
// Quota schrumpft die Affinitaets-Maske NICHT — deshalb ist das der eigene Wert.
#include "cpu_runtime.h"
#include <thread>
#include <set>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef __linux__
#include <sched.h>
#include <unistd.h>
#endif

namespace {

// Untergrenze 1: nie 0 zurueckgeben, sonst teilt eine Sizing-Formel durch 0.
int32_t at_least_one(long v) { return v >= 1 ? static_cast<int32_t>(v) : 1; }

int32_t read_logical() {
#ifdef __linux__
    cpu_set_t set;
    CPU_ZERO(&set);
    if (sched_getaffinity(0, sizeof(set), &set) == 0) {
        int n = CPU_COUNT(&set);
        if (n >= 1) return n;
    }
#endif
    return at_least_one(static_cast<long>(std::thread::hardware_concurrency()));
}

// Physische Kerne: (package_id, core_id)-Paare der erlaubten CPUs zaehlen.  Nur
// CPUs aus der Affinitaets-Maske, damit core_count <= logical_thread_count gilt
// (sonst meldete ein getaskseter Prozess mehr Kerne als nutzbare Threads).
int32_t read_physical(int32_t logical) {
#ifdef __linux__
    cpu_set_t set;
    CPU_ZERO(&set);
    bool have_mask = (sched_getaffinity(0, sizeof(set), &set) == 0);
    std::set<std::pair<long, long>> cores;
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
        if (have_mask && !CPU_ISSET(cpu, &set)) continue;
        char path[160];
        long pkg = 0, core = -1;
        snprintf(path, sizeof(path),
                 "/sys/devices/system/cpu/cpu%d/topology/core_id", cpu);
        FILE* f = fopen(path, "r");
        if (!f) continue;                      // CPU existiert nicht (oder kein sysfs)
        if (fscanf(f, "%ld", &core) != 1) core = -1;
        fclose(f);
        if (core < 0) continue;
        snprintf(path, sizeof(path),
                 "/sys/devices/system/cpu/cpu%d/topology/physical_package_id", cpu);
        f = fopen(path, "r");
        if (f) {
            if (fscanf(f, "%ld", &pkg) != 1) pkg = 0;
            fclose(f);
        }
        cores.insert({pkg, core});
    }
    if (!cores.empty()) {
        int32_t n = static_cast<int32_t>(cores.size());
        return n <= logical ? n : logical;     // Topologie darf die Maske nie uebersteigen
    }
#endif
    return logical;                            // Topologie unlesbar → konservativ
}

// cgroup v2: "/sys/fs/cgroup/cpu.max" = "<quota|max> <period>".
// cgroup v1: cpu.cfs_quota_us (-1 = unlimited) / cpu.cfs_period_us.
// Rueckgabe: ganze Threads, abgerundet, min 1; ohne Quota → logical.
int32_t read_quota(int32_t logical) {
#ifdef __linux__
    if (FILE* f = fopen("/sys/fs/cgroup/cpu.max", "r")) {
        char quota[64] = {0};
        long period = 0;
        int got = fscanf(f, "%63s %ld", quota, &period);
        fclose(f);
        if (got == 2 && period > 0 && strcmp(quota, "max") != 0) {
            long q = strtol(quota, nullptr, 10);
            if (q > 0) {
                long threads = q / period;     // abrunden, aber nie unter 1
                int32_t n = at_least_one(threads);
                return n <= logical ? n : logical;
            }
        }
    }
    long v1_quota = -1, v1_period = 0;
    if (FILE* f = fopen("/sys/fs/cgroup/cpu/cpu.cfs_quota_us", "r")) {
        if (fscanf(f, "%ld", &v1_quota) != 1) v1_quota = -1;
        fclose(f);
    }
    if (FILE* f = fopen("/sys/fs/cgroup/cpu/cpu.cfs_period_us", "r")) {
        if (fscanf(f, "%ld", &v1_period) != 1) v1_period = 0;
        fclose(f);
    }
    if (v1_quota > 0 && v1_period > 0) {
        int32_t n = at_least_one(v1_quota / v1_period);
        return n <= logical ? n : logical;
    }
#endif
    return logical;                            // kein Quota gesetzt
}

struct CpuInfo {
    int32_t logical;
    int32_t physical;
    int32_t quota;
    CpuInfo() {
        logical  = at_least_one(read_logical());
        physical = at_least_one(read_physical(logical));
        quota    = at_least_one(read_quota(logical));
    }
};

// Funktions-lokales static: thread-safe initialisiert (C++11 magic static), also
// aus jedem Thread aufrufbar, ohne eigenen Mutex (spec/47 Kategorie 2).
const CpuInfo& info() {
    static const CpuInfo cached;
    return cached;
}

} // namespace

extern "C" int32_t aiml_sys_core_count(void)           { return info().physical; }
extern "C" int32_t aiml_sys_logical_thread_count(void) { return info().logical; }
extern "C" int32_t aiml_sys_quota_thread_count(void)   { return info().quota; }
