// spec/23 §5: sys.exe / process — spawn options, capture, timeout, detach (durable monitor),
// sys.process_attach. process is a Class-4 resource (spec/61): aiml_process_release at drop.
#include "process_runtime.h"
#include "pipeline_runtime.h"
#include "../../logger.h"
#include <spawn.h>
#include <unistd.h>
#include <linux/close_range.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <atomic>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

extern char** environ;
extern "C" char* aiml_str_dup(const char* s);
extern "C" void aiml_str_decref(char* s);

struct AimlList { int32_t size; int32_t capacity; void* data; };
struct AimlMapRaw { int32_t size; int32_t capacity; uint8_t* occ; void* keys; void* vals; };

namespace {

constexpr int64_t kTermGraceMs = 2000;

struct ProcessHandle {
    pid_t pid = -1;          // job pid (detach: the job, not the monitor)
    pid_t child = -1;        // our direct child to reap (job, or the monitor); -1 when attached
    bool detached = false;
    bool attached = false;
    bool finished = false;
    int32_t returncode = -1;
    int fds[2] = {-1, -1};   // capture pipes (stdout, stderr), read ends
    std::string bufs[2];     // captured, not yet consumed
    char* cached[2] = {nullptr, nullptr};   // rc strings handed out by stdout_buffer/stderr_buffer
    int pidfd = -1;          // readable once `child` ends (sys.wait_any); -1 = not opened / unsupported
    int64_t deadline = -1;   // monotonic ms, parent-enforced timeout (non-detached)
    int64_t term_sent = -1;
    bool timed_out = false;
    std::string status_file; // detach/attach
};

std::mutex g_orphan_mtx;
std::vector<pid_t> g_orphans;   // released handles whose child was not reaped yet

int64_t mono_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void reap_orphans() {
    std::lock_guard<std::mutex> lock(g_orphan_mtx);
    for (size_t i = 0; i < g_orphans.size();) {
        int st;
        pid_t r = waitpid(g_orphans[i], &st, WNOHANG);
        if (r == g_orphans[i] || (r < 0 && errno == ECHILD)) g_orphans.erase(g_orphans.begin() + i);
        else ++i;
    }
}

int32_t decode_status(int st) {
    if (WIFEXITED(st)) return WEXITSTATUS(st);
    if (WIFSIGNALED(st)) return 128 + WTERMSIG(st);
    return -1;
}

std::vector<std::string> list_strings(void* listPtr) {
    std::vector<std::string> out;
    if (!listPtr) return out;
    auto* l = static_cast<AimlList*>(listPtr);
    char** data = static_cast<char**>(l->data);
    for (int32_t i = 0; i < l->size; ++i) out.emplace_back(data && data[i] ? data[i] : "");
    return out;
}

std::vector<std::string> env_strings(void* mapPtr, bool& has_env) {
    std::vector<std::string> out;
    has_env = mapPtr != nullptr;
    if (!mapPtr) return out;
    auto* m = static_cast<AimlMapRaw*>(mapPtr);
    char** keys = static_cast<char**>(m->keys);
    char** vals = static_cast<char**>(m->vals);
    for (int32_t i = 0; i < m->size; ++i)
        if (keys && keys[i]) out.push_back(std::string(keys[i]) + "=" + (vals && vals[i] ? vals[i] : ""));
    return out;
}

std::string absolute(const std::string& p) {
    if (p.empty() || p[0] == '/') return p;
    char cwd[4096];
    if (!getcwd(cwd, sizeof(cwd))) return p;
    return std::string(cwd) + "/" + p;
}

// PATH lookup done before fork (execv in the monitor's child stays async-signal-safe).
std::string resolve_program(const std::string& prog, const std::vector<std::string>& envv, bool has_env) {
    if (prog.find('/') != std::string::npos) return prog;
    std::string path;
    if (has_env) {
        for (auto& e : envv) if (e.rfind("PATH=", 0) == 0) path = e.substr(5);
    }
    if (path.empty()) { const char* p = getenv("PATH"); path = p ? p : "/usr/bin:/bin"; }
    size_t start = 0;
    while (start <= path.size()) {
        size_t end = path.find(':', start);
        std::string dir = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
        std::string cand = (dir.empty() ? "." : dir) + "/" + prog;
        if (access(cand.c_str(), X_OK) == 0) return cand;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return "";
}

std::vector<char*> cvec(std::vector<std::string>& v) {
    std::vector<char*> out;
    for (auto& s : v) out.push_back(const_cast<char*>(s.c_str()));
    out.push_back(nullptr);
    return out;
}

void default_signals_in_child() {   // async-signal-safe
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_DFL;
    for (int s = 1; s < NSIG; ++s) if (s != SIGKILL && s != SIGSTOP) sigaction(s, &sa, nullptr);
    sigset_t none;
    sigemptyset(&none);
    sigprocmask(SIG_SETMASK, &none, nullptr);
}

void write_int(int fd, long v) {   // async-signal-safe decimal + "\n"
    char buf[24]; int n = 0; bool neg = v < 0; unsigned long u = neg ? -v : v;
    char tmp[24]; int t = 0;
    do { tmp[t++] = (char)('0' + u % 10); u /= 10; } while (u);
    if (neg) buf[n++] = '-';
    while (t) buf[n++] = tmp[--t];
    buf[n++] = '\n';
    ssize_t w = write(fd, buf, n); (void)w;
}

// Read whatever is available on the open capture pipes (block up to wait_ms on the first poll).
void drain(ProcessHandle* h, int wait_ms) {
    for (;;) {
        struct pollfd pf[2]; int idx[2]; int n = 0;
        for (int i = 0; i < 2; ++i) if (h->fds[i] >= 0) { pf[n].fd = h->fds[i]; pf[n].events = POLLIN; pf[n].revents = 0; idx[n++] = i; }
        if (n == 0) return;
        int r = poll(pf, n, wait_ms);
        if (r <= 0) return;
        wait_ms = 0;
        for (int k = 0; k < n; ++k) {
            if (!(pf[k].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            char buf[8192];
            ssize_t got = read(pf[k].fd, buf, sizeof(buf));
            if (got > 0) h->bufs[idx[k]].append(buf, (size_t)got);
            else if (got == 0 || errno != EINTR) { close(h->fds[idx[k]]); h->fds[idx[k]] = -1; }
        }
    }
}

bool attached_status(ProcessHandle* h) {   // status file present → finished
    if (h->status_file.empty()) return false;
    FILE* f = fopen(h->status_file.c_str(), "re");
    if (!f) return false;
    int code = -1;
    if (fscanf(f, "%d", &code) != 1) code = -1;
    fclose(f);
    h->returncode = code;
    h->finished = true;
    return true;
}

bool pid_alive(pid_t pid) {
    return pid > 0 && (kill(pid, 0) == 0 || errno == EPERM);
}

void enforce_timeout(ProcessHandle* h) {
    if (h->deadline < 0 || h->finished) return;
    int64_t now = mono_ms();
    if (now < h->deadline) return;
    if (h->term_sent < 0) { kill(h->pid, SIGTERM); h->term_sent = now; h->timed_out = true; }
    else if (now - h->term_sent >= kTermGraceMs) kill(h->pid, SIGKILL);
}

// One non-blocking status step; true while running.
bool step(ProcessHandle* h) {
    if (h->finished) return false;
    if (h->attached) {
        if (attached_status(h)) return false;
        if (pid_alive(h->pid)) return true;
        // the monitor writes the status right after the job ends — give it a moment
        for (int i = 0; i < 50; ++i) {
            if (attached_status(h)) return false;
            usleep(10000);
        }
        h->finished = true; h->returncode = -1;
        return false;
    }
    enforce_timeout(h);
    int st;
    pid_t r = waitpid(h->child, &st, WNOHANG);
    if (r == h->child) {
        h->finished = true;
        h->returncode = h->timed_out ? 124 : decode_status(st);
        aiml_step_forget_child(static_cast<int64_t>(h->child));   // F62-40: its pid may be reused now
        h->child = -1;
        return false;
    }
    if (r < 0 && errno == ECHILD) { h->finished = true; h->child = -1; return false; }
    return true;
}

ProcessHandle* spawn_detached(std::vector<std::string>& argv, const std::string& cwd,
                              std::vector<std::string>& envv, bool has_env, int32_t timeout,
                              const std::string& out_path, const std::string& err_path,
                              const std::string& status_file) {
    std::string prog = resolve_program(argv[0], envv, has_env);
    if (prog.empty()) return nullptr;
    std::vector<char*> cargv = cvec(argv);
    std::vector<char*> cenv = has_env ? cvec(envv) : std::vector<char*>();
    char* const* envp = has_env ? cenv.data() : environ;
    std::string out_abs = absolute(out_path), err_abs = absolute(err_path), st_abs = absolute(status_file);
    std::string st_tmp = st_abs.empty() ? "" : st_abs + ".tmp";
    int report[2];
    if (pipe2(report, O_CLOEXEC) != 0) return nullptr;

    pid_t monitor = fork();
    if (monitor < 0) { close(report[0]); close(report[1]); return nullptr; }
    if (monitor == 0) {
        // ── monitor: own session, keeps none of the parent's descriptors ──
        setsid();
        int keep = report[1];
        if (keep > 3) close_range(3, keep - 1, 0);
        close_range(keep + 1, ~0U, 0);
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) { dup2(devnull, 0); dup2(devnull, 1); dup2(devnull, 2); if (devnull > 2) close(devnull); }
        pid_t job = fork();
        if (job == 0) {
            setpgid(0, 0);
            default_signals_in_child();
            close(keep);
            int ofd = out_abs.empty() ? -1 : open(out_abs.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (ofd >= 0) { dup2(ofd, 1); if (ofd > 2) close(ofd); }
            if (!err_abs.empty()) {
                if (err_abs == out_abs) dup2(1, 2);
                else { int efd = open(err_abs.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644); if (efd >= 0) { dup2(efd, 2); if (efd > 2) close(efd); } }
            }
            if (!cwd.empty() && chdir(cwd.c_str()) != 0) _exit(127);
            execve(prog.c_str(), cargv.data(), envp);
            _exit(127);
        }
        long reported = job > 0 ? (long)job : -1L;
        ssize_t w = write(keep, &reported, sizeof(reported)); (void)w;
        close(keep);
        if (job < 0) _exit(127);
        setpgid(job, job);
        int st = 0; bool timed_out = false;
        struct timespec start; clock_gettime(CLOCK_MONOTONIC, &start);
        long t0 = start.tv_sec * 1000 + start.tv_nsec / 1000000, term_at = -1;
        for (;;) {
            pid_t r = waitpid(job, &st, timeout >= 0 ? WNOHANG : 0);
            if (r == job) break;
            if (r < 0 && errno != EINTR) { st = 0; break; }
            if (timeout >= 0) {
                struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
                long ms = now.tv_sec * 1000 + now.tv_nsec / 1000000;
                if (term_at < 0 && ms - t0 >= timeout) { kill(-job, SIGTERM); term_at = ms; timed_out = true; }
                else if (term_at >= 0 && ms - term_at >= kTermGraceMs) kill(-job, SIGKILL);
                struct timespec nap = {0, 10 * 1000 * 1000};
                nanosleep(&nap, nullptr);
            }
        }
        int code = timed_out ? 124 : (WIFEXITED(st) ? WEXITSTATUS(st) : WIFSIGNALED(st) ? 128 + WTERMSIG(st) : 255);
        if (!st_tmp.empty()) {
            int sfd = open(st_tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (sfd >= 0) { write_int(sfd, code); fsync(sfd); close(sfd); rename(st_tmp.c_str(), st_abs.c_str()); }
        }
        _exit(code & 0xff);
    }
    close(report[1]);
    long job_pid = -1;
    ssize_t got;
    do { got = read(report[0], &job_pid, sizeof(job_pid)); } while (got < 0 && errno == EINTR);
    close(report[0]);
    if (got != (ssize_t)sizeof(job_pid) || job_pid <= 0) {
        int st; waitpid(monitor, &st, 0);
        return nullptr;
    }
    auto* h = new ProcessHandle();
    h->pid = (pid_t)job_pid;
    h->child = monitor;
    h->detached = true;
    h->status_file = st_abs;
    return h;
}

ProcessHandle* spawn_attached(std::vector<std::string>& argv, const std::string& cwd,
                              std::vector<std::string>& envv, bool has_env, int32_t timeout, bool capture,
                              const std::string& in_path, const std::string& out_path, const std::string& err_path) {
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    sigset_t defaults; sigemptyset(&defaults);
    for (int s = 1; s < NSIG; ++s) if (s != SIGKILL && s != SIGSTOP) sigaddset(&defaults, s);
    sigset_t none; sigemptyset(&none);
    posix_spawnattr_setsigdefault(&attr, &defaults);
    posix_spawnattr_setsigmask(&attr, &none);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK);

    int pipes[2][2] = {{-1, -1}, {-1, -1}};
    bool redirect[2] = {!out_path.empty(), !err_path.empty()};
    for (int i = 0; i < 2; ++i) {
        if (redirect[i] || !capture) continue;
        if (pipe2(pipes[i], O_CLOEXEC) != 0) { pipes[i][0] = pipes[i][1] = -1; continue; }
        posix_spawn_file_actions_adddup2(&fa, pipes[i][1], i + 1);
    }
    // redirect paths are relative to the parent's cwd: open before the chdir
    // stdin: checked here, not only in the child — a posix_spawn that falls back to
    // fork+exec (e.g. under valgrind) cannot report a failed file action
    if (!in_path.empty()) {
        if (access(in_path.c_str(), R_OK) != 0) {
            posix_spawn_file_actions_destroy(&fa);
            posix_spawnattr_destroy(&attr);
            return nullptr;
        }
        posix_spawn_file_actions_addopen(&fa, 0, in_path.c_str(), O_RDONLY, 0);
    }
    if (redirect[0]) posix_spawn_file_actions_addopen(&fa, 1, out_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (redirect[1]) {
        if (redirect[0] && err_path == out_path) posix_spawn_file_actions_adddup2(&fa, 1, 2);
        else posix_spawn_file_actions_addopen(&fa, 2, err_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    }
    if (!cwd.empty()) posix_spawn_file_actions_addchdir_np(&fa, cwd.c_str());
    posix_spawn_file_actions_addclosefrom_np(&fa, 3);

    std::vector<char*> cargv = cvec(argv);
    std::vector<char*> cenv = has_env ? cvec(envv) : std::vector<char*>();
    pid_t pid = -1;
    int rc = posix_spawnp(&pid, cargv[0], &fa, &attr, cargv.data(), has_env ? cenv.data() : environ);
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&attr);
    for (int i = 0; i < 2; ++i) if (pipes[i][1] >= 0) close(pipes[i][1]);
    if (rc != 0) {
        for (int i = 0; i < 2; ++i) if (pipes[i][0] >= 0) close(pipes[i][0]);
        return nullptr;
    }
    auto* h = new ProcessHandle();
    h->pid = pid;
    h->child = pid;
    h->fds[0] = pipes[0][0];
    h->fds[1] = pipes[1][0];
    if (timeout >= 0) h->deadline = mono_ms() + timeout;
    return h;
}

}  // namespace

extern "C" void* aiml_process_spawn(void* argvList, const char* cwd, void* env, int32_t shell,
                                    int32_t timeout, int32_t capture, const char* stdin_path,
                                    const char* stdout_path, const char* stderr_path, int32_t detach,
                                    const char* status_file);

// spec/62 F62-27: sys.exe with an ExeOptions value.  The one difference to the option
// bag: a model has no "unset", so an empty `env` from it means inherit (spec/23's default).
extern "C" void* aiml_process_spawn_opts(void* argvList, const char* cwd, void* env, int32_t shell,
                                         int32_t timeout, int32_t capture, const char* stdin_path,
                                         const char* stdout_path, const char* stderr_path,
                                         int32_t detach, const char* status_file,
                                         int32_t env_empty_inherits) {
    if (env && env_empty_inherits && static_cast<AimlMapRaw*>(env)->size == 0) env = nullptr;
    return aiml_process_spawn(argvList, cwd, env, shell, timeout, capture, stdin_path, stdout_path,
                              stderr_path, detach, status_file);
}

extern "C" void* aiml_process_spawn(void* argvList, const char* cwd, void* env, int32_t shell,
                                    int32_t timeout, int32_t capture, const char* stdin_path,
                                    const char* stdout_path, const char* stderr_path, int32_t detach,
                                    const char* status_file) {
    reap_orphans();
    std::vector<std::string> argv = list_strings(argvList);
    if (argv.empty()) return nullptr;
    if (shell) {
        std::vector<std::string> sh = {"/bin/sh", "-c", argv[0]};
        sh.insert(sh.end(), argv.begin() + 1, argv.end());
        argv.swap(sh);
    }
    // spec/62 F62-29: with ExeOptions as a value these are runtime values, so what
    // spec/23 §5.4 rejects at compile time for literals is rejected here.
    if (capture && detach) {
        LOG_ERROR("sys.exe: capture: true cannot be combined with detach: true (a detached process "
                  "outlives its pipes — redirect with stdout/stderr, spec/23 §5.4, spec/62 F62-29)");
        return nullptr;
    }
    if (status_file && *status_file && !detach) {
        LOG_ERROR("sys.exe: status_file requires detach: true (spec/23 §5.4, spec/62 F62-29)");
        return nullptr;
    }
    bool has_env = false;
    std::vector<std::string> envv = env_strings(env, has_env);
    std::string cwd_s = cwd ? cwd : "", in_s = stdin_path ? stdin_path : "",
                out_s = stdout_path ? stdout_path : "",
                err_s = stderr_path ? stderr_path : "", st_s = status_file ? status_file : "";
    ProcessHandle* h = detach
        ? spawn_detached(argv, cwd_s, envv, has_env, timeout, out_s, err_s, st_s)
        : spawn_attached(argv, cwd_s, envv, has_env, timeout, capture != 0, in_s, out_s, err_s);
    if (!h) LOG_DEBUG("[Runtime] sys.exe: spawn failed for " + argv[0]);
    // spec/62 F62-40: a pipeline step with a time limit kills what it started.
    else aiml_step_note_child(static_cast<int64_t>(h->pid), h->child != h->pid ? static_cast<int64_t>(h->child) : 0);
    return h;
}

// Legacy entry (pre spec/23 §5 option bag).
extern "C" void* sys_exe_runtime(void* argvList, char* cwd, void*, bool, int32_t, bool) {
    return aiml_process_spawn(argvList, cwd, nullptr, 0, -1, 0, nullptr, nullptr, nullptr, 0, nullptr);
}

extern "C" void* aiml_process_attach(int64_t pid, const char* status_file) {
    auto* h = new ProcessHandle();
    h->pid = (pid_t)pid;
    h->attached = true;
    h->status_file = absolute(status_file ? status_file : "");
    return h;
}

extern "C" bool aiml_process_running(void* handle) {
    auto* h = static_cast<ProcessHandle*>(handle);
    if (!h) return false;
    drain(h, 0);
    return step(h);
}

extern "C" int32_t process_wait(void* handle) {
    auto* h = static_cast<ProcessHandle*>(handle);
    if (!h) return -1;
    while (step(h)) {
        if (!h->attached && h->deadline < 0 && h->fds[0] < 0 && h->fds[1] < 0) {
            int st;   // nothing to drain or enforce: block in waitpid
            pid_t r;
            do { r = waitpid(h->child, &st, 0); } while (r < 0 && errno == EINTR);
            h->finished = true;
            h->returncode = r == h->child ? decode_status(st) : -1;
            aiml_step_forget_child(static_cast<int64_t>(h->child));   // F62-40
            h->child = -1;
            break;
        }
        if (h->fds[0] >= 0 || h->fds[1] >= 0) drain(h, 20);
        else usleep(20000);
    }
    drain(h, 0);
    reap_orphans();
    return h->returncode;
}

extern "C" void process_kill(void* handle) {
    auto* h = static_cast<ProcessHandle*>(handle);
    if (!h || h->finished || h->pid <= 0) return;
    kill(h->detached || h->attached ? -h->pid : h->pid, SIGKILL);
}

// process.signal(sig): 0 sent, -1 not (finished, unknown process or signal).
extern "C" int32_t aiml_process_signal(void* handle, int32_t sig) {
    auto* h = static_cast<ProcessHandle*>(handle);
    if (!h || h->finished || h->pid <= 0 || sig <= 0 || sig >= NSIG) return -1;
    return kill(h->detached || h->attached ? -h->pid : h->pid, sig) == 0 ? 0 : -1;
}

extern "C" int64_t aiml_process_pid(void* handle) {
    auto* h = static_cast<ProcessHandle*>(handle);
    return h ? (int64_t)h->pid : -1;
}

extern "C" int32_t aiml_process_returncode(void* handle) {
    auto* h = static_cast<ProcessHandle*>(handle);
    return h && h->finished ? h->returncode : -1;
}

// Property read: an rc string owned by the handle (like a string field) — readers that keep
// it incref; the handle drops its reference on refresh and at release.
extern "C" char* aiml_process_buffer(void* handle, int32_t which) {
    auto* h = static_cast<ProcessHandle*>(handle);
    if (!h || which < 0 || which > 1) return aiml_str_dup("");
    drain(h, 0);
    if (h->cached[which] && h->bufs[which] == h->cached[which]) return h->cached[which];
    if (h->cached[which]) aiml_str_decref(h->cached[which]);
    h->cached[which] = aiml_str_dup(h->bufs[which].c_str());
    return h->cached[which];
}

extern "C" char* aiml_process_read_line(void* handle, int32_t which) {
    auto* h = static_cast<ProcessHandle*>(handle);
    if (!h || which < 0 || which > 1) return aiml_str_dup("");
    for (;;) {
        std::string& b = h->bufs[which];
        size_t nl = b.find('\n');
        if (nl != std::string::npos) {
            char* line = aiml_str_dup(b.substr(0, nl).c_str());
            b.erase(0, nl + 1);
            return line;
        }
        if (h->fds[which] < 0) {
            char* rest = aiml_str_dup(b.c_str());
            b.clear();
            return rest;
        }
        enforce_timeout(h);
        drain(h, 50);
    }
}

extern "C" bool aiml_process_eof(void* handle) {
    auto* h = static_cast<ProcessHandle*>(handle);
    if (!h) return true;
    drain(h, 0);
    if (step(h)) return false;
    drain(h, 0);
    return h->bufs[0].empty() && h->bufs[1].empty();
}

extern "C" void aiml_process_release(void* handle) {
    auto* h = static_cast<ProcessHandle*>(handle);
    if (!h) return;
    for (int i = 0; i < 2; ++i) {
        if (h->fds[i] >= 0) close(h->fds[i]);
        if (h->cached[i]) aiml_str_decref(h->cached[i]);
    }
    if (h->pidfd >= 0) close(h->pidfd);
    if (!h->finished && h->child > 0) {
        int st;
        if (waitpid(h->child, &st, WNOHANG) != h->child) {
            std::lock_guard<std::mutex> lock(g_orphan_mtx);
            g_orphans.push_back(h->child);
        }
    }
    delete h;
}

/* ── spec/23 §5.7: caught signals ──────────────────────────────────────────── */

namespace {

std::atomic<int> g_caught_signal{0};     // last caught, cleared by sys.caught_signal
std::atomic<unsigned> g_signal_count{0}; // every caught signal, for sys.wait_any

void record_signal(int sig) {            // async-signal-safe: two lock-free stores
    g_caught_signal.store(sig);
    g_signal_count.fetch_add(1);
}

int signal_number(const std::string& name) {
    static const struct { const char* name; int sig; } kKnown[] = {
        {"INT", SIGINT}, {"TERM", SIGTERM}, {"HUP", SIGHUP}, {"QUIT", SIGQUIT},
        {"USR1", SIGUSR1}, {"USR2", SIGUSR2}, {"PIPE", SIGPIPE}};
    for (const auto& k : kKnown) if (name == k.name) return k.sig;
    return 0;
}

}  // namespace

extern "C" int32_t aiml_sys_catch_signals(const char* names) {
    std::vector<int> sigs;
    std::string list = names ? names : "";
    size_t start = 0;
    while (start <= list.size()) {
        size_t comma = list.find(',', start);
        std::string one = list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        size_t a = one.find_first_not_of(" \t"), b = one.find_last_not_of(" \t");
        one = a == std::string::npos ? "" : one.substr(a, b - a + 1);
        if (one.rfind("SIG", 0) == 0) one = one.substr(3);
        if (!one.empty()) {
            int sig = signal_number(one);
            if (!sig) return -1;
            sigs.push_back(sig);
        }
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = record_signal;
    sa.sa_flags = SA_RESTART;          // blocking reads go on; poll(2) still returns EINTR
    sigemptyset(&sa.sa_mask);
    for (int sig : sigs) sigaction(sig, &sa, nullptr);
    return 0;
}

extern "C" int32_t aiml_sys_caught_signal(void) {
    return g_caught_signal.exchange(0);
}

/* ── spec/23 §5.5: sys.wait_any ────────────────────────────────────────────── */

// The index of an ended process in a list<process> (lowest index), -1 on
// timeout, an empty list or a caught signal.  Capture pipes are drained and
// timeouts enforced while waiting; the wait itself is poll(2) on pidfds.
extern "C" int32_t aiml_sys_wait_any(void* listPtr, int32_t timeout_ms) {
    auto* list = static_cast<AimlList*>(listPtr);
    if (!list || list->size <= 0) return -1;
    auto** procs = static_cast<ProcessHandle**>(list->data);
    const unsigned signals_before = g_signal_count.load();
    const int64_t deadline = timeout_ms < 0 ? -1 : mono_ms() + timeout_ms;
    std::vector<struct pollfd> fds;
    for (;;) {
        bool need_ticks = false;          // something no descriptor reports (timeouts, attached jobs)
        fds.clear();
        for (int32_t i = 0; i < list->size; ++i) {
            ProcessHandle* h = procs[i];
            if (!h) continue;
            drain(h, 0);
            if (!step(h)) { reap_orphans(); return i; }
            if (h->attached || h->deadline >= 0) need_ticks = true;
            if (h->child > 0 && h->pidfd < 0) h->pidfd = (int)syscall(SYS_pidfd_open, h->child, 0);
            if (h->pidfd >= 0) fds.push_back({h->pidfd, POLLIN, 0});
            else need_ticks = true;
            for (int k = 0; k < 2; ++k) if (h->fds[k] >= 0) fds.push_back({h->fds[k], POLLIN, 0});
        }
        if (g_signal_count.load() != signals_before) return -1;
        int wait = -1;
        if (deadline >= 0) {
            int64_t left = deadline - mono_ms();
            if (left <= 0) return -1;
            wait = (int)std::min<int64_t>(left, 1 << 30);
        }
        if (need_ticks && (wait < 0 || wait > 20)) wait = 20;
        if (poll(fds.data(), (nfds_t)fds.size(), wait) < 0 && errno == EINTR
            && g_signal_count.load() != signals_before) return -1;
    }
}

// spec/23: absolute path of the running executable, "" when unknown; runtime-owned.
/* ── spec/23 §5.6: sys.require_tools ──────────────────────────────────────────
 * What the environment must bring along, in ONE answer.  Three forms:
 *   "git"            a program in PATH
 *   "gcc>=13"        a program with a minimum version (first number of the first line)
 *   "lib:yaml-cpp"   a library via pkg-config
 *
 * EVERYTHING missing is named (F23-40) — a check that stops at the first error
 * produces three runs instead of one.  ONE place for this question (F23-43):
 * `requires:` in aiml spec/62 and the cosmos agent call the same code.
 */
static bool rt_tool_in_path(const std::string& name) {
    if (name.find('/') != std::string::npos)
        return access(name.c_str(), X_OK) == 0;
    const char* pathEnv = getenv("PATH");
    if (!pathEnv) return false;
    std::string paths(pathEnv);
    size_t from = 0;
    while (from <= paths.size()) {
        size_t cut = paths.find(':', from);
        std::string dir = paths.substr(from, cut == std::string::npos ? std::string::npos : cut - from);
        if (!dir.empty()) {
            std::string full = dir + "/" + name;
            if (access(full.c_str(), X_OK) == 0) return true;
        }
        if (cut == std::string::npos) break;
        from = cut + 1;
    }
    return false;
}

/* First number of the first output line of `<tool> --version`; -1 = none. */
static long rt_tool_version(const std::string& tool) {
    std::string cmd = tool + " --version 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return -1;
    char line[512] = {0};
    char* got = fgets(line, sizeof(line), pipe);
    pclose(pipe);
    if (!got) return -1;
    for (const char* p = line; *p; ++p) {
        if (*p >= '0' && *p <= '9') return strtol(p, nullptr, 10);
    }
    return -1;
}

extern "C" char* aiml_sys_require_tools(const char* spec) {
    std::string missing;
    if (!spec || !*spec) return aiml_str_dup("");
    std::string all(spec);
    bool havePkgConfig = rt_tool_in_path("pkg-config");
    size_t from = 0;
    while (from <= all.size()) {
        size_t cut = all.find(',', from);
        std::string one = all.substr(from, cut == std::string::npos ? std::string::npos : cut - from);
        if (cut == std::string::npos) from = all.size() + 1; else from = cut + 1;
        /* trimmen */
        while (!one.empty() && (one.front() == ' ' || one.front() == '\t')) one.erase(one.begin());
        while (!one.empty() && (one.back() == ' ' || one.back() == '\t' || one.back() == '\n')) one.pop_back();
        if (one.empty()) continue;

        if (one.rfind("lib:", 0) == 0) {
            std::string lib = one.substr(4);
            if (!havePkgConfig) {
                missing += " " + one + "(pkg-config missing)";   /* F23-41 */
                continue;
            }
            std::string cmd = "pkg-config --exists " + lib + " 2>/dev/null";
            if (system(cmd.c_str()) != 0) missing += " " + one;
            continue;
        }

        size_t ge = one.find(">=");
        if (ge == std::string::npos) {
            if (!rt_tool_in_path(one)) missing += " " + one;
            continue;
        }
        std::string tool = one.substr(0, ge);
        std::string want = one.substr(ge + 2);
        if (!rt_tool_in_path(tool)) { missing += " " + one; continue; }
        long have = rt_tool_version(tool);
        long need = strtol(want.c_str(), nullptr, 10);
        if (have < 0) {
            missing += " " + one + "(hat ?)";                  /* F23-42 */
        } else if (have < need) {
            missing += " " + one + "(hat " + std::to_string(have) + ")";
        }
    }
    return aiml_str_dup(missing.c_str());
}

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
