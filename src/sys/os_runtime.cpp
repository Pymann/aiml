// Working directory, environment and file helpers (spec/23 §5.8).

#include "os_runtime.h"
#include "string_runtime.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <mutex>
#include <set>
#include <string>
#include <vector>

extern "C" int32_t aiml_sys_chdir(const char* dir) {
    return dir && *dir && chdir(dir) == 0 ? 0 : -1;
}

extern "C" int32_t aiml_sys_setenv(const char* name, const char* value) {
    if (!name || !*name || strchr(name, '=')) return -1;
    return setenv(name, value ? value : "", 1) == 0 ? 0 : -1;
}

extern "C" int32_t aiml_sys_unsetenv(const char* name) {
    if (!name || !*name || strchr(name, '=')) return -1;
    return unsetenv(name) == 0 ? 0 : -1;
}

extern "C" int32_t aiml_sys_isatty(int32_t fd) {
    return fd >= 0 && isatty(fd) ? 1 : 0;
}

extern "C" char* aiml_io_realpath(const char* p) {
    char buf[PATH_MAX];
    if (!p || !*p || !realpath(p, buf)) return aiml_str_dup("");
    return aiml_str_dup(buf);
}

extern "C" char* aiml_io_mkdtemp(const char* prefix) {
    std::string tmpl = std::string(prefix && *prefix ? prefix : "/tmp/aiml-") + "XXXXXX";
    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');
    if (!mkdtemp(buf.data())) return aiml_str_dup("");
    return aiml_str_dup(buf.data());
}

extern "C" int32_t aiml_io_copy(const char* src, const char* dst) {
    if (!src || !dst || !*src || !*dst) return -1;
    struct stat st;
    if (stat(src, &st) != 0 || !S_ISREG(st.st_mode)) return -1;
    int in = open(src, O_RDONLY | O_CLOEXEC);
    if (in < 0) return -1;
    std::string part = std::string(dst) + ".part";
    int out = open(part.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, st.st_mode & 07777);
    if (out < 0) { close(in); return -1; }
    std::vector<char> buf(1 << 20);
    bool ok = true;
    for (;;) {
        ssize_t got = read(in, buf.data(), buf.size());
        if (got == 0) break;
        if (got < 0) { ok = false; break; }
        for (ssize_t off = 0; off < got;) {
            ssize_t put = write(out, buf.data() + off, (size_t)(got - off));
            if (put <= 0) { ok = false; break; }
            off += put;
        }
        if (!ok) break;
    }
    close(in);
    if (fchmod(out, st.st_mode & 07777) != 0) ok = false;   // the umask must not narrow it
    if (close(out) != 0) ok = false;
    if (ok && rename(part.c_str(), dst) == 0) return 0;
    unlink(part.c_str());
    return -1;
}

// A lock id is the descriptor holding the flock; only ids handed out here can be unlocked
// (an arbitrary number must not close somebody else's descriptor).
static std::mutex g_lock_mtx;
static std::set<int> g_locks;

extern "C" int32_t aiml_io_lock(const char* p) {
    if (!p || !*p) return -1;
    int fd = open(p, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0) return -1;
    int rc;
    do { rc = flock(fd, LOCK_EX); } while (rc != 0 && errno == EINTR);
    if (rc != 0) { close(fd); return -1; }
    std::lock_guard<std::mutex> guard(g_lock_mtx);
    g_locks.insert(fd);
    return fd;
}

extern "C" int32_t aiml_io_unlock(int32_t id) {
    {
        std::lock_guard<std::mutex> guard(g_lock_mtx);
        if (!g_locks.erase(id)) return -1;
    }
    flock(id, LOCK_UN);
    return close(id) == 0 ? 0 : -1;
}
