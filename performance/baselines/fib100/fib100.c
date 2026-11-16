/* Baseline fuer performance/cases/fib100.aiml (spec/70 §7, F70-60: gleicher
 * Algorithmus, gleiche Maschine, gleicher Lauf).  uint64_t, weil signierter
 * Ueberlauf in C UB ist -- die Arbeitslast ist dieselbe: 100 Additionen je
 * Runde, Summe als Wachwert, Kalibrierung auf >= 500 ms, sieben Proben. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static uint64_t fib_round(void) {
    uint64_t a = 0, b = 1, acc = 0;
    for (int i = 0; i < 100; i++) { acc += a; uint64_t n = a + b; a = b; b = n; }
    return acc;
}
static uint64_t fib_at(int k) {
    uint64_t a = 0, b = 1;
    for (int i = 0; i < k; i++) { uint64_t n = a + b; a = b; b = n; }
    return a;
}
static long mono_ms(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}
static volatile uint64_t g_sink;
static long bench(long reps) {
    long t0 = mono_ms(); uint64_t sink = 0;
    for (long k = 0; k < reps; k++) sink += fib_round();
    long el = mono_ms() - t0; g_sink = sink;
    if (sink == 0) { puts("sink=0"); exit(1); }
    return el;
}
static int cmp(const void *x, const void *y) {
    long a = *(const long*)x, b = *(const long*)y; return (a > b) - (a < b);
}
int main(void) {
    if (fib_at(10) != 55) { puts("FAIL fib(10)"); return 1; }
    printf("fib92=%llu\n", (unsigned long long)fib_at(92));
    long reps = 1000, el = bench(reps);
    while (el < 500) { reps *= 2; el = bench(reps); }
    printf("reps=%ld calib_ms=%ld\n", reps, el);
    long s[7];
    for (int j = 0; j < 7; j++) { s[j] = bench(reps); printf("sample_ms=%ld\n", s[j]); }
    qsort(s, 7, sizeof(long), cmp);
    long med = s[3], ns_round = med * 1000000L / reps;
    printf("median_ms=%ld min_ms=%ld max_ms=%ld reps=%ld ns_per_round=%ld ns_per_step=%ld\n",
           med, s[0], s[6], reps, ns_round, ns_round / 100);
    return 0;
}
