#pragma once
#include <cstdint>

// spec/23 §5: sys.exe / process runtime.
extern "C" {
    void* aiml_process_spawn(void* argvList, const char* cwd, void* env, int32_t shell, int32_t timeout,
                             int32_t capture, const char* stdin_path, const char* stdout_path,
                             const char* stderr_path, int32_t detach, const char* status_file);
    void* sys_exe_runtime(void* argvList, char* cwd, void* env, bool shell, int32_t timeout, bool capture);
    void* aiml_process_attach(int64_t pid, const char* status_file);
    bool aiml_process_running(void* handle);
    int32_t process_wait(void* handle);
    void process_kill(void* handle);
    int32_t aiml_process_signal(void* handle, int32_t sig);          // 0 sent, -1 not
    int32_t aiml_sys_wait_any(void* procs, int32_t timeout_ms);      // list<process> → index, -1
    int32_t aiml_sys_catch_signals(const char* names);               // "INT,TERM" → 0, -1 unknown
    int32_t aiml_sys_caught_signal(void);                            // last caught (cleared), 0 none
    int64_t aiml_process_pid(void* handle);
    int32_t aiml_process_returncode(void* handle);
    char* aiml_process_buffer(void* handle, int32_t which);      // 0 stdout, 1 stderr; handle-owned
    char* aiml_process_read_line(void* handle, int32_t which);   // fresh owned string
    bool aiml_process_eof(void* handle);
    void aiml_process_release(void* handle);
    const char* aiml_sys_exe_path(void);
    // spec/23 §5.6: was die Umgebung mitbringen muss; "" = alles da.
    char* aiml_sys_require_tools(const char* spec);
}
