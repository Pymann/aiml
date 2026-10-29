// Working directory, environment and file helpers (spec/23 §5.8).
#pragma once
#include <cstdint>

extern "C" {
int32_t aiml_sys_chdir(const char* dir);
int32_t aiml_sys_setenv(const char* name, const char* value);
int32_t aiml_sys_unsetenv(const char* name);
int32_t aiml_sys_isatty(int32_t fd);
char*   aiml_io_realpath(const char* p);       // managed string, "" if missing
char*   aiml_io_mkdtemp(const char* prefix);   // managed string, "" on failure
int32_t aiml_io_copy(const char* src, const char* dst);
int32_t aiml_io_lock(const char* p);           // lock id >= 0, -1
int32_t aiml_io_unlock(int32_t id);
}
