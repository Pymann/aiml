#pragma once
#include <cstdint>

extern "C" {
    // time.now() -> milliseconds since epoch
    int64_t time_now_ms();
    
    // time.sleep_ms()
    void time_sleep_ms(int32_t ms);
}
