#pragma once
#include <cstdint>

extern "C" {
    // sys.exe runtime helper
    // argvList: Pointer to AimlList object
    // cwd: string or null
    // env: pointer to Map (ignored for now) or null
    // shell: bool
    // timeout: int32
    // capture: bool
    void* sys_exe_runtime(void* argvList, char* cwd, void* env, bool shell, int32_t timeout, bool capture);
    
    int32_t process_wait(void* handle);
    void process_kill(void* handle);
    // Add more if needed (read_line, eof)
}
