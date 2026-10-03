#include "time_runtime.h"
#include "string_runtime.h"   // aiml_str_dup
#include <sys/time.h>
#include <unistd.h>
#include <ctime>
#include <cstdio>
#include <iostream>

#ifdef __EMSCRIPTEN__
#  include <emscripten/emscripten.h>
#endif

extern "C" int64_t time_now_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + (int64_t)tv.tv_usec / 1000;
}

extern "C" int64_t time_mono_ms() {
#ifdef __EMSCRIPTEN__
    return (int64_t)emscripten_get_now();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + (int64_t)ts.tv_nsec / 1000000;
#endif
}

extern "C" char* time_format_utc(int64_t ms) {
    time_t secs = (time_t)(ms / 1000);
    struct tm tmv;
    gmtime_r(&secs, &tmv);
    char buf[32];
    // "YYYY-MM-DD HH:MM" (UTC) — stabil in nativem + WASM-Build (gmtime_r/strftime).
    if (strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tmv) == 0) buf[0] = '\0';
    return aiml_str_dup(buf);
}

extern "C" void time_sleep_ms(int32_t ms) {
#ifdef __EMSCRIPTEN__
    // Yields control to the JS event loop. Requires -sASYNCIFY at link time.
    emscripten_sleep((unsigned int)ms);
#else
    usleep(ms * 1000);
#endif
}
