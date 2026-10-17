#pragma once
#include <cstdint>

// spec/23 §5: sys.exe / process runtime.
extern "C" {
    void* aiml_process_spawn(void* argvList, const char* cwd, void* env, int32_t shell, int32_t timeout,
                             int32_t capture, const char* stdout_path, const char* stderr_path,
                             int32_t detach, const char* status_file);
    void* sys_exe_runtime(void* argvList, char* cwd, void* env, bool shell, int32_t timeout, bool capture);
    void* aiml_process_attach(int64_t pid, const char* status_file);
    bool aiml_process_running(void* handle);
    int32_t process_wait(void* handle);
    void process_kill(void* handle);
    int64_t aiml_process_pid(void* handle);
    int32_t aiml_process_returncode(void* handle);
    char* aiml_process_buffer(void* handle, int32_t which);      // 0 stdout, 1 stderr; handle-owned
    char* aiml_process_read_line(void* handle, int32_t which);   // fresh owned string
    bool aiml_process_eof(void* handle);
    void aiml_process_release(void* handle);
    const char* aiml_sys_exe_path(void);
}
