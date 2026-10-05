#include "process_runtime.h"
#include "../../logger.h"
#include <spawn.h>
#include <unistd.h>
#include <sys/wait.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

// Structure matches GenericList in list_impl.h
struct AimlList {
    int32_t size;
    int32_t capacity;
    void* data; // char** for List<String>
};

struct ProcessHandle {
    pid_t pid;
    // Future: pipe FDs for capture
};

extern "C" void* sys_exe_runtime(void* argvListPtr, char* cwd, void* env, bool shell, int32_t timeout, bool capture) {
    if (!argvListPtr) {
        LOG_ERROR("[Runtime] sys_exe_runtime: argvListPtr is null");
        return nullptr;
    }
    AimlList* list = (AimlList*)argvListPtr;
    LOG_DEBUG("[Runtime] sys_exe_runtime: list size=" + std::to_string(list->size) + ", cap=" + std::to_string(list->capacity));
    
    // Build argv
    int argc = list->size;
    std::vector<char*> argv(argc + 1);
    char** srcData = (char**)list->data;
    
    for(int i=0; i<argc; ++i) {
        if (srcData[i]) {
            LOG_DEBUG("[Runtime] arg[" + std::to_string(i) + "]: " + std::string(srcData[i]));
            argv[i] = srcData[i];
        } else {
             LOG_ERROR("[Runtime] arg[" + std::to_string(i) + "] is null!");
             argv[i] = (char*)"";
        }
    }
    argv[argc] = nullptr;
    
    // posix_spawn setup
    posix_spawn_file_actions_t file_actions;
    posix_spawn_file_actions_init(&file_actions);
    
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    
    // Handle CWD
    #ifdef _GNU_SOURCE
    if (cwd && strlen(cwd) > 0) {
        posix_spawn_file_actions_addchdir_np(&file_actions, cwd);
    }
    #endif
    
    // Explicit declaration if not provided by headers
    extern char **environ;

    pid_t pid;
    if (argv[0]) {
        LOG_DEBUG("[Runtime] Calling posix_spawnp for: " + std::string(argv[0]));
    } else {
        LOG_ERROR("[Runtime] argv[0] is null");
        return nullptr;
    }

    // Use posix_spawnp to search PATH
    int status = posix_spawnp(&pid, argv[0], &file_actions, &attr, argv.data(), environ);
    
    posix_spawn_file_actions_destroy(&file_actions);
    posix_spawnattr_destroy(&attr);
    
    if (status != 0) {
        LOG_ERROR("[Runtime] posix_spawnp failed with status: " + std::to_string(status));
        return nullptr;
    }
    
    LOG_DEBUG("[Runtime] Process started with PID: " + std::to_string(pid));
    ProcessHandle* handle = new ProcessHandle();
    handle->pid = pid;
    return handle;
}

extern "C" int32_t process_wait(void* handlePtr) {
    if (!handlePtr) return -1;
    ProcessHandle* handle = (ProcessHandle*)handlePtr;
    
    int status;
    waitpid(handle->pid, &status, 0);
    
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return -1;
}

extern "C" void process_kill(void* handlePtr) {
    if (!handlePtr) return;
    ProcessHandle* handle = (ProcessHandle*)handlePtr;
    kill(handle->pid, SIGKILL);
}

// spec/23: absolute path of the running executable, "" when unknown; runtime-owned.
extern "C" const char* aiml_sys_exe_path(void) {
    static char path[4096] = {0};
    static bool done = false;
    if (!done) {
        done = true;
#if defined(__linux__)
        ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
        path[n > 0 ? n : 0] = '\0';
#endif
    }
    return path;
}
