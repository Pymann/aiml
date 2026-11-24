// netfs_runtime.cpp — network drives via client libraries (spec/63).
//
// Route A from §63.1: the protocol runs IN THE PROCESS, without a mount and without root.
// That is the normal route, because an agent does not run as root (cosmos
// doc/spec/94) and a kernel mount would demand exactly that.
//
// Binding is done via **dlopen**, not via the linker: otherwise every
// aiml program would have a build dependency on Samba, even if it never touches a
// network drive.  If the library is missing, there is a clear message
// instead of a link error on every build.
#include "string_runtime.h"

#include <dlfcn.h>
#include <cstring>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

namespace {

// ── Die Teilmenge von libsmbclient, die wir brauchen ────────────────────────
using smbc_get_auth_data_fn = void (*)(const char* srv, const char* shr,
                                       char* wg, int wglen,
                                       char* un, int unlen,
                                       char* pw, int pwlen);
using fn_smbc_init     = int  (*)(smbc_get_auth_data_fn fn, int debug);
using fn_smbc_opendir  = int  (*)(const char* durl);
using fn_smbc_readdir  = void*(*)(unsigned int dh);
using fn_smbc_closedir = int  (*)(int dh);
using fn_smbc_open     = int  (*)(const char* furl, int flags, unsigned short mode);
using fn_smbc_read     = ssize_t (*)(int fd, void* buf, size_t bufsize);
using fn_smbc_write    = ssize_t (*)(int fd, const void* buf, size_t bufsize);
using fn_smbc_close    = int  (*)(int fd);

struct SmbApi {
    void* handle = nullptr;
    fn_smbc_init     init     = nullptr;
    fn_smbc_opendir  opendir  = nullptr;
    fn_smbc_readdir  readdir  = nullptr;
    fn_smbc_closedir closedir = nullptr;
    fn_smbc_open     open     = nullptr;
    fn_smbc_read     read     = nullptr;
    fn_smbc_write    write    = nullptr;
    fn_smbc_close    close    = nullptr;
    bool initialised = false;
};

// Credentials.  They live ONLY here and are never logged, never written into an
// argument (spec/63 F63-6) — the same rule as for the
// enrollment token and the sudo password.
struct Cred {
    std::string domain;
    std::string user;
    std::string pass;
};

std::mutex        g_mtx;
SmbApi            g_smb;
std::vector<Cred> g_creds;          // 1-basierte Griffe; 0 = anonym
int32_t           g_active_cred = 0;
std::string       g_last_error;

void set_error(const std::string& msg) { g_last_error = msg; }

// The callback of libsmbclient.  It gets what is in the handle — and
// nobody else sees it.
void auth_cb(const char*, const char*,
             char* wg, int wglen, char* un, int unlen, char* pw, int pwlen) {
    std::lock_guard<std::mutex> lock(g_mtx);
    const Cred* c = nullptr;
    if (g_active_cred > 0 && (size_t)g_active_cred <= g_creds.size())
        c = &g_creds[(size_t)g_active_cred - 1];
    auto copy_in = [](char* dst, int cap, const std::string& src) {
        if (!dst || cap <= 0) return;
        const size_t n = src.size() < (size_t)cap - 1 ? src.size() : (size_t)cap - 1;
        memcpy(dst, src.data(), n);
        dst[n] = '\0';
    };
    copy_in(wg, wglen, c ? c->domain : std::string());
    copy_in(un, unlen, c ? c->user   : std::string("guest"));
    copy_in(pw, pwlen, c ? c->pass   : std::string());
}

bool load_smb() {
    if (g_smb.handle) return true;
    // The name with SONAME, not the developer symlink: the latter comes from
    // a -dev package that need not be installed on a node.
    g_smb.handle = dlopen("libsmbclient.so.0", RTLD_NOW | RTLD_LOCAL);
    if (!g_smb.handle) {
        // dlerror() clears the error it returns: call it ONCE. Calling it again
        // for the message gave nullptr, and std::string + nullptr crashed.
        const char* why = dlerror();
        set_error(std::string("libsmbclient cannot be loaded: ") + (why ? why : "?"));
        return false;
    }
    auto sym = [&](const char* n) { return dlsym(g_smb.handle, n); };
    g_smb.init     = (fn_smbc_init)     sym("smbc_init");
    g_smb.opendir  = (fn_smbc_opendir)  sym("smbc_opendir");
    g_smb.readdir  = (fn_smbc_readdir)  sym("smbc_readdir");
    g_smb.closedir = (fn_smbc_closedir) sym("smbc_closedir");
    g_smb.open     = (fn_smbc_open)     sym("smbc_open");
    g_smb.read     = (fn_smbc_read)     sym("smbc_read");
    g_smb.write    = (fn_smbc_write)    sym("smbc_write");
    g_smb.close    = (fn_smbc_close)    sym("smbc_close");
    if (!g_smb.init || !g_smb.opendir || !g_smb.open) {
        set_error("libsmbclient loaded, but symbols are missing");
        dlclose(g_smb.handle);
        g_smb.handle = nullptr;
        return false;
    }
    return true;
}

// The directory entry of libsmbclient.  Only the fields we read —
// the layout has been stable since SMBCLIENT_0.1.0.
struct smbc_dirent_head {
    unsigned int smbc_type;
    unsigned int dirlen;
    unsigned int commentlen;
    char*        comment;
    unsigned int namelen;
    char         name[1];
};

std::string g_base_url;   // "smb://host/share"

} // namespace

extern "C" {

// Credentials from a file: lines "user=", "password=", "domain=".
// From a FILE and not from a string in the source text (F63-6): what
// is in argv is in the node's process list.
int32_t aiml_netfs_cred_from_file(const char* path) {
    std::lock_guard<std::mutex> lock(g_mtx);
    if (!path) { set_error("cred_from_file: no path"); return 0; }
    FILE* f = fopen(path, "r");
    if (!f) { set_error(std::string("cred_from_file: not readable: ") + path); return 0; }
    Cred c;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        std::string s(line);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        const size_t eq = s.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = s.substr(0, eq), v = s.substr(eq + 1);
        if      (k == "user" || k == "username") c.user   = v;
        else if (k == "password")                c.pass   = v;
        else if (k == "domain" || k == "workgroup") c.domain = v;
    }
    fclose(f);
    if (c.user.empty()) { set_error("cred_from_file: no user= in the file"); return 0; }
    g_creds.push_back(c);
    return (int32_t)g_creds.size();
}

// Connecting here means: check the scheme, load the library, activate the
// credentials.  A protocol handshake only happens on the first access —
// that is the case with SMB too, and claiming anything else would be
// false security.
int32_t aiml_netfs_connect(const char* url, int32_t cred) {
    std::lock_guard<std::mutex> lock(g_mtx);
    if (!url) { set_error("connect: no URL"); return 0; }
    const std::string u(url);
    if (u.rfind("smb://", 0) != 0) {
        // F63-4: an unknown scheme is an ERROR, not a fallback to local
        // io — a silent fallback would write into the wrong file system.
        set_error("connect: only smb:// is supported, not '" + u + "'");
        return 0;
    }
    if (!load_smb()) return 0;
    if (!g_smb.initialised) {
        if (g_smb.init(auth_cb, 0) != 0) { set_error("smbc_init failed"); return 0; }
        g_smb.initialised = true;
    }
    g_active_cred = cred;
    g_base_url    = u;
    while (!g_base_url.empty() && g_base_url.back() == '/') g_base_url.pop_back();
    set_error("");
    return 1;
}

char* aiml_netfs_last_error(void) {
    std::lock_guard<std::mutex> lock(g_mtx);
    char* out = aiml_str_alloc(g_last_error.size());
    if (!g_last_error.empty()) memcpy(out, g_last_error.data(), g_last_error.size());
    return out;
}

// Listing.  Returned as one string with "\n" per entry — deliberately plain,
// so that the first step gets by without a list ABI; the list comes when the
// test server is in place (spec/63 §63.4 item 2).
char* aiml_netfs_list(const char* rel) {
    std::lock_guard<std::mutex> lock(g_mtx);
    if (!g_smb.handle || g_base_url.empty()) { set_error("list: not connected"); return aiml_str_alloc(0); }
    std::string url = g_base_url + (rel && *rel ? (rel[0] == '/' ? rel : std::string("/") + rel) : "");
    const int dh = g_smb.opendir(url.c_str());
    if (dh < 0) { set_error("list: opendir failed for " + url); return aiml_str_alloc(0); }
    std::string out;
    while (void* raw = g_smb.readdir((unsigned)dh)) {
        auto* de = static_cast<smbc_dirent_head*>(raw);
        const std::string nm(de->name, de->namelen);
        if (nm == "." || nm == "..") continue;
        out += nm;
        out += "\n";
    }
    g_smb.closedir(dh);
    char* res = aiml_str_alloc(out.size());
    if (!out.empty()) memcpy(res, out.data(), out.size());
    return res;
}

char* aiml_netfs_read_file(const char* rel) {
    std::lock_guard<std::mutex> lock(g_mtx);
    if (!g_smb.handle || g_base_url.empty()) { set_error("read_file: not connected"); return aiml_str_alloc(0); }
    std::string url = g_base_url + (rel && rel[0] == '/' ? rel : std::string("/") + (rel ? rel : ""));
    const int fd = g_smb.open(url.c_str(), 0 /*O_RDONLY*/, 0);
    if (fd < 0) { set_error("read_file: open failed for " + url); return aiml_str_alloc(0); }
    std::string data;
    char buf[65536];
    for (;;) {
        const ssize_t n = g_smb.read(fd, buf, sizeof(buf));
        if (n <= 0) break;
        data.append(buf, (size_t)n);
    }
    g_smb.close(fd);
    char* res = aiml_str_alloc(data.size());
    if (!data.empty()) memcpy(res, data.data(), data.size());
    return res;
}

int32_t aiml_netfs_write_file(const char* rel, const char* data) {
    std::lock_guard<std::mutex> lock(g_mtx);
    if (!g_smb.handle || g_base_url.empty()) { set_error("write_file: not connected"); return -1; }
    std::string url = g_base_url + (rel && rel[0] == '/' ? rel : std::string("/") + (rel ? rel : ""));
    // O_WRONLY|O_CREAT|O_TRUNC = 0x241 on Linux.
    const int fd = g_smb.open(url.c_str(), 0x241, 0644);
    if (fd < 0) { set_error("write_file: open failed for " + url); return -1; }
    const size_t len = data ? strlen(data) : 0;
    const ssize_t n = len ? g_smb.write(fd, data, len) : 0;
    g_smb.close(fd);
    if (n < 0 || (size_t)n != len) { set_error("write_file: incompletely written"); return -1; }
    return 0;
}

void aiml_netfs_close(void) {
    std::lock_guard<std::mutex> lock(g_mtx);
    g_base_url.clear();
    g_active_cred = 0;
}

// Is a network protocol available at all?  That way a program can
// decide instead of failing on an error message.
int32_t aiml_netfs_available(void) {
    std::lock_guard<std::mutex> lock(g_mtx);
    return load_smb() ? 1 : 0;
}

} // extern "C"
