#include "time_runtime.h"
#include <sys/time.h>
#include <unistd.h>
#include <iostream>

#ifdef __EMSCRIPTEN__
#  include <emscripten/emscripten.h>
#endif

extern "C" int64_t time_now_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + (int64_t)tv.tv_usec / 1000;
}

extern "C" void time_sleep_ms(int32_t ms) {
#ifdef __EMSCRIPTEN__
    // Yields control to the JS event loop. Requires -sASYNCIFY at link time.
    emscripten_sleep((unsigned int)ms);
#else
    usleep(ms * 1000);
#endif
}
