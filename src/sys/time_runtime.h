#pragma once
#include <cstdint>

extern "C" {
    // time.now() -> milliseconds since epoch
    int64_t time_now_ms();
    
    // time.sleep_ms()
    void time_sleep_ms(int32_t ms);

    // time.format_utc(ms) -> "YYYY-MM-DD HH:MM" (UTC).  Returns an AIML-managed
    // string (aiml_str_dup).  Reusable formatter for any epoch-ms timestamp.
    char* time_format_utc(int64_t ms);
}
