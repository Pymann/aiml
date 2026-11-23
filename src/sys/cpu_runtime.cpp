// spec/23 §4.1 — host parallelism.  Three questions, three functions:
//   core_count            physical cores            → CPU-bound pools
//   logical_thread_count  affinity mask             → what can run at the same time
//   quota_thread_count    cgroup cpu.max            → what the container allows
// All values are read ONCE per process and cached (spec: deterministic
// within a process, category 2 = callable from any thread).  A
// quota does NOT shrink the affinity mask — that is why it is a value of its own.
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

// Lower bound 1: never return 0, otherwise a sizing formula divides by 0.
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

// Physical cores: count the (package_id, core_id) pairs of the allowed CPUs.  Only
// CPUs from the affinity mask, so that core_count <= logical_thread_count holds
// (otherwise a taskset process would report more cores than usable threads).
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
        if (!f) continue;                      // CPU does not exist (or no sysfs)
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
        return n <= logical ? n : logical;     // topology must never exceed the mask
    }
#endif
    return logical;                            // Topologie unlesbar → konservativ
}

// cgroup v2: "/sys/fs/cgroup/cpu.max" = "<quota|max> <period>".
// cgroup v1: cpu.cfs_quota_us (-1 = unlimited) / cpu.cfs_period_us.
// Return: whole threads, rounded down, min 1; without a quota → logical.
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
                long threads = q / period;     // round down, but never below 1
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
    return logical;                            // no quota set
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

// Function-local static: initialised thread-safely (C++11 magic static), so
// callable from any thread without a mutex of its own (spec/47 category 2).
const CpuInfo& info() {
    static const CpuInfo cached;
    return cached;
}

} // namespace

extern "C" int32_t aiml_sys_core_count(void)           { return info().physical; }
extern "C" int32_t aiml_sys_logical_thread_count(void) { return info().logical; }
extern "C" int32_t aiml_sys_quota_thread_count(void)   { return info().quota; }
