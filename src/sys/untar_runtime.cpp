// compress.untar / compress.unar (spec/33_2 §3.7a): extract any tar (plain, gzip,
// xz, zstd — by magic) and ar archives, safely.
//
// Safety: every path is split into components and walked from the target
// directory with openat(O_NOFOLLOW): a ".." component, an absolute path or a
// path through a symbolic link is refused, so nothing is written outside
// `dst_dir`, whatever the archive says.

#include "untar_runtime.h"

#include <fcntl.h>
#include <lzma.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>
#include <zstd.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

enum Rc { kOk = 0, kArgs = -1, kOpen = -2, kCorrupt = -3, kUnsafe = -4, kWrite = -5 };

/* ── decompressing input stream ───────────────────────────────────────── */

class Input {
public:
    virtual ~Input() = default;
    // Up to n bytes into buf; 0 = end, -1 = error.
    virtual long read(unsigned char* buf, size_t n) = 0;

    // Exactly n bytes; false = end or error first.
    bool read_full(unsigned char* buf, size_t n) {
        size_t got = 0;
        while (got < n) {
            long r = read(buf + got, n - got);
            if (r <= 0) return false;
            got += (size_t)r;
        }
        return true;
    }
    bool skip(uint64_t n) {
        unsigned char tmp[65536];
        while (n > 0) {
            size_t want = n < sizeof(tmp) ? (size_t)n : sizeof(tmp);
            if (!read_full(tmp, want)) return false;
            n -= want;
        }
        return true;
    }
};

// The raw file with the first bytes already peeked.
class FileInput : public Input {
public:
    explicit FileInput(FILE* f) : f_(f) {}
    ~FileInput() override { if (f_) fclose(f_); }
    long read(unsigned char* buf, size_t n) override {
        size_t got = fread(buf, 1, n, f_);
        if (got == 0 && ferror(f_)) return -1;
        return (long)got;
    }
private:
    FILE* f_;
};

// A decoder fed from a raw input in 64 KiB blocks.
class Decoder : public Input {
public:
    explicit Decoder(std::unique_ptr<Input> raw) : raw_(std::move(raw)), in_(1 << 16) {}
protected:
    // Refill in_ when empty; false at the end of the raw input.
    bool refill() {
        if (pos_ < len_) return true;
        long r = raw_->read(in_.data(), in_.size());
        if (r <= 0) { eof_ = true; return false; }
        pos_ = 0;
        len_ = (size_t)r;
        return true;
    }
    std::unique_ptr<Input> raw_;
    std::vector<unsigned char> in_;
    size_t pos_ = 0, len_ = 0;
    bool eof_ = false;
};

class GzipInput : public Decoder {
public:
    explicit GzipInput(std::unique_ptr<Input> raw) : Decoder(std::move(raw)) {
        memset(&z_, 0, sizeof(z_));
        ok_ = inflateInit2(&z_, 16 + MAX_WBITS) == Z_OK;
    }
    ~GzipInput() override { inflateEnd(&z_); }
    long read(unsigned char* buf, size_t n) override {
        if (!ok_) return -1;
        z_.next_out = buf;
        z_.avail_out = (uInt)n;
        while (z_.avail_out == n) {
            if (done_) return 0;
            if (pos_ >= len_ && !refill()) return -1;           // truncated
            z_.next_in = in_.data() + pos_;
            z_.avail_in = (uInt)(len_ - pos_);
            int rc = inflate(&z_, Z_NO_FLUSH);
            pos_ = len_ - z_.avail_in;
            if (rc == Z_STREAM_END) {
                // another gzip member may follow (concatenated gzip)
                if (pos_ < len_ || refill()) inflateReset(&z_);
                else done_ = true;
            } else if (rc != Z_OK && rc != Z_BUF_ERROR) {
                return -1;
            }
        }
        return (long)(n - z_.avail_out);
    }
private:
    z_stream z_;
    bool ok_ = false, done_ = false;
};

class XzInput : public Decoder {
public:
    explicit XzInput(std::unique_ptr<Input> raw) : Decoder(std::move(raw)) {
        ok_ = lzma_stream_decoder(&s_, UINT64_MAX, LZMA_CONCATENATED) == LZMA_OK;
    }
    ~XzInput() override { lzma_end(&s_); }
    long read(unsigned char* buf, size_t n) override {
        if (!ok_) return -1;
        s_.next_out = buf;
        s_.avail_out = n;
        while (s_.avail_out == n) {
            if (done_) return 0;
            lzma_action action = LZMA_RUN;
            if (pos_ >= len_ && !refill()) action = LZMA_FINISH;
            s_.next_in = in_.data() + pos_;
            s_.avail_in = action == LZMA_FINISH ? 0 : len_ - pos_;
            lzma_ret rc = lzma_code(&s_, action);
            pos_ = len_ - s_.avail_in;
            if (rc == LZMA_STREAM_END) done_ = true;
            else if (rc != LZMA_OK) return -1;
            else if (action == LZMA_FINISH && s_.avail_out == n) return -1;   // truncated
        }
        return (long)(n - s_.avail_out);
    }
private:
    lzma_stream s_ = LZMA_STREAM_INIT;
    bool ok_ = false, done_ = false;
};

class ZstdInput : public Decoder {
public:
    explicit ZstdInput(std::unique_ptr<Input> raw) : Decoder(std::move(raw)), d_(ZSTD_createDStream()) {
        if (d_) ZSTD_initDStream(d_);
    }
    ~ZstdInput() override { if (d_) ZSTD_freeDStream(d_); }
    long read(unsigned char* buf, size_t n) override {
        if (!d_) return -1;
        ZSTD_outBuffer out = {buf, n, 0};
        while (out.pos == 0) {
            if (pos_ >= len_ && !refill()) return last_ == 0 ? 0 : -1;   // mid-frame end = truncated
            ZSTD_inBuffer in = {in_.data() + pos_, len_ - pos_, 0};
            last_ = ZSTD_decompressStream(d_, &out, &in);
            pos_ += in.pos;
            if (ZSTD_isError(last_)) return -1;
        }
        return (long)out.pos;
    }
private:
    ZSTD_DStream* d_;
    size_t last_ = 0;
};

// The same bytes, with `head` (already read for the magic check) in front.
class Rewound : public Input {
public:
    Rewound(std::unique_ptr<Input> rest, const unsigned char* head, size_t n)
        : rest_(std::move(rest)), head_(head, head + n) {}
    long read(unsigned char* buf, size_t n) override {
        if (at_ < head_.size()) {
            size_t k = std::min(n, head_.size() - at_);
            memcpy(buf, head_.data() + at_, k);
            at_ += k;
            return (long)k;
        }
        return rest_->read(buf, n);
    }
private:
    std::unique_ptr<Input> rest_;
    std::vector<unsigned char> head_;
    size_t at_ = 0;
};

std::unique_ptr<Input> open_any(const char* path) {
    FILE* f = fopen(path, "rbe");
    if (!f) return nullptr;
    std::unique_ptr<Input> raw(new FileInput(f));
    unsigned char magic[6] = {0};
    long n = raw->read(magic, sizeof(magic));
    if (n < 0) return nullptr;
    std::unique_ptr<Input> file(new Rewound(std::move(raw), magic, (size_t)n));
    if (n >= 2 && magic[0] == 0x1f && magic[1] == 0x8b) return std::unique_ptr<Input>(new GzipInput(std::move(file)));
    if (n >= 6 && memcmp(magic, "\xFD" "7zXZ\0", 6) == 0) return std::unique_ptr<Input>(new XzInput(std::move(file)));
    if (n >= 4 && magic[0] == 0x28 && magic[1] == 0xB5 && magic[2] == 0x2F && magic[3] == 0xFD)
        return std::unique_ptr<Input>(new ZstdInput(std::move(file)));
    return file;
}

/* ── safe paths below the target directory ───────────────────────────── */

// "a/./b/" → {"a","b"}; false for an absolute path or a ".." component.
bool components(const std::string& path, std::vector<std::string>& out) {
    out.clear();
    if (path.empty() || path[0] == '/') return false;
    size_t start = 0;
    while (start <= path.size()) {
        size_t slash = path.find('/', start);
        std::string part = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        if (part == "..") return false;
        if (!part.empty() && part != ".") out.push_back(part);
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    return true;
}

// The directory holding the last component, opened without following links
// (missing directories are created).  -1 = unsafe or not creatable.
int open_parent(int root, const std::vector<std::string>& parts) {
    int fd = dup(root);
    for (size_t i = 0; fd >= 0 && i + 1 < parts.size(); ++i) {
        int next = openat(fd, parts[i].c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (next < 0 && errno == ENOENT && mkdirat(fd, parts[i].c_str(), 0755) == 0)
            next = openat(fd, parts[i].c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        close(fd);
        fd = next;                    // ELOOP / ENOTDIR (a link or a file in the way) → -1
    }
    return fd;
}

// Whatever is at parent/leaf goes, unless it is a directory we keep.
void clear_slot(int parent, const char* leaf) {
    struct stat st;
    if (fstatat(parent, leaf, &st, AT_SYMLINK_NOFOLLOW) == 0 && !S_ISDIR(st.st_mode))
        unlinkat(parent, leaf, 0);
}

uint64_t number(const unsigned char* field, size_t len) {
    if (field[0] & 0x80) {                                   // base-256 (GNU, star)
        uint64_t v = field[0] & 0x3f;
        for (size_t i = 1; i < len; ++i) v = (v << 8) | field[i];
        return v;
    }
    uint64_t v = 0;
    size_t i = 0;
    while (i < len && (field[i] == ' ' || field[i] == 0)) ++i;
    for (; i < len && field[i] >= '0' && field[i] <= '7'; ++i) v = v * 8 + (uint64_t)(field[i] - '0');
    return v;
}

std::string text(const unsigned char* field, size_t len) {
    size_t n = 0;
    while (n < len && field[n]) ++n;
    return std::string((const char*)field, n);
}

bool checksum_ok(const unsigned char* h) {
    uint64_t want = number(h + 148, 8), sum = 0;
    for (int i = 0; i < 512; ++i) sum += (i >= 148 && i < 156) ? ' ' : h[i];
    return sum == want;
}

// pax records "len key=value\n" → path / linkpath / size
void pax_records(const std::string& data, std::string& path, std::string& link, uint64_t& size, bool& has_size) {
    size_t at = 0;
    while (at < data.size()) {
        size_t space = data.find(' ', at);
        if (space == std::string::npos) break;
        size_t len = (size_t)strtoull(data.c_str() + at, nullptr, 10);
        if (len == 0 || at + len > data.size()) break;
        std::string rec = data.substr(space + 1, at + len - space - 2);   // without the "\n"
        size_t eq = rec.find('=');
        if (eq != std::string::npos) {
            std::string key = rec.substr(0, eq), value = rec.substr(eq + 1);
            if (key == "path") path = value;
            else if (key == "linkpath") link = value;
            else if (key == "size") { size = strtoull(value.c_str(), nullptr, 10); has_size = true; }
        }
        at += len;
    }
}

struct DirTimes { std::string path; mode_t mode; struct timespec mtime; };

class Extractor {
public:
    Extractor(Input& in, int root) : in_(in), root_(root) {}

    int run() {
        unsigned char h[512];
        int zeros = 0;
        while (true) {
            if (!in_.read_full(h, 512)) return zeros > 0 || seen_ ? finish() : kCorrupt;
            bool empty = true;
            for (int i = 0; i < 512 && empty; ++i) empty = h[i] == 0;
            if (empty) { if (++zeros == 2) return finish(); continue; }
            zeros = 0;
            if (!checksum_ok(h)) return kCorrupt;
            seen_ = true;
            int rc = entry(h);
            if (rc != kOk) return rc;
        }
    }

private:
    Input& in_;
    int root_;
    bool seen_ = false;
    std::string long_name_, long_link_, pax_path_, pax_link_;
    uint64_t pax_size_ = 0;
    bool pax_has_size_ = false;
    std::vector<DirTimes> dirs_;

    bool read_data(uint64_t size, std::string& out) {
        if (size > (64u << 20)) return false;             // a header record, not file data
        out.resize((size_t)size);
        if (size && !in_.read_full((unsigned char*)&out[0], (size_t)size)) return false;
        return in_.skip((512 - size % 512) % 512);
    }

    int entry(const unsigned char* h) {
        char type = (char)h[156];
        uint64_t size = number(h + 124, 12);
        if (type == 'L' || type == 'K' || type == 'x' || type == 'g') {
            std::string data;
            if (!read_data(size, data)) return kCorrupt;
            if (type == 'L') long_name_ = text((const unsigned char*)data.data(), data.size());
            else if (type == 'K') long_link_ = text((const unsigned char*)data.data(), data.size());
            else if (type == 'x') pax_records(data, pax_path_, pax_link_, pax_size_, pax_has_size_);
            return kOk;                                     // 'g' (global) is skipped
        }
        std::string name = text(h, 100);
        if (memcmp(h + 257, "ustar", 5) == 0) {
            std::string prefix = text(h + 345, 155);
            if (!prefix.empty()) name = prefix + "/" + name;
        }
        std::string link = text(h + 157, 100);
        if (!long_name_.empty()) name = long_name_;
        if (!long_link_.empty()) link = long_link_;
        if (!pax_path_.empty()) name = pax_path_;
        if (!pax_link_.empty()) link = pax_link_;
        if (pax_has_size_) size = pax_size_;
        long_name_.clear(); long_link_.clear(); pax_path_.clear(); pax_link_.clear();
        pax_has_size_ = false;

        mode_t mode = (mode_t)(number(h + 100, 8) & 0777);
        struct timespec mtime = {(time_t)number(h + 136, 12), 0};
        uint64_t padded = size + (512 - size % 512) % 512;

        std::vector<std::string> parts;
        if (!components(name, parts)) return kUnsafe;
        if (parts.empty()) return in_.skip(padded) ? kOk : kCorrupt;   // "./" itself
        int parent = open_parent(root_, parts);
        if (parent < 0) return kUnsafe;
        const char* leaf = parts.back().c_str();
        int rc = kOk;
        switch (type) {
            case '0': case '\0': case '7': rc = file(parent, leaf, size, padded, mode, mtime); break;
            case '5':
                if (mkdirat(parent, leaf, 0755) != 0 && errno != EEXIST) rc = kWrite;
                else {
                    struct stat st;
                    if (fstatat(parent, leaf, &st, AT_SYMLINK_NOFOLLOW) != 0 || !S_ISDIR(st.st_mode)) rc = kUnsafe;
                    else dirs_.push_back({name, mode, mtime});
                }
                if (rc == kOk && !in_.skip(padded)) rc = kCorrupt;
                break;
            case '2':
                clear_slot(parent, leaf);
                if (symlinkat(link.c_str(), parent, leaf) != 0) rc = kWrite;
                else {
                    struct timespec times[2] = {{0, UTIME_OMIT}, mtime};
                    utimensat(parent, leaf, times, AT_SYMLINK_NOFOLLOW);
                }
                if (rc == kOk && !in_.skip(padded)) rc = kCorrupt;
                break;
            case '1': {
                std::vector<std::string> target;
                if (!components(link, target) || target.empty()) { rc = kUnsafe; break; }
                int tparent = open_parent(root_, target);
                if (tparent < 0) { rc = kUnsafe; break; }
                clear_slot(parent, leaf);
                if (linkat(tparent, target.back().c_str(), parent, leaf, 0) != 0) rc = kWrite;
                close(tparent);
                if (rc == kOk && !in_.skip(padded)) rc = kCorrupt;
                break;
            }
            default:                                        // devices, FIFOs, unknown: skipped
                if (!in_.skip(padded)) rc = kCorrupt;
                break;
        }
        close(parent);
        return rc;
    }

    int file(int parent, const char* leaf, uint64_t size, uint64_t padded, mode_t mode, struct timespec mtime) {
        clear_slot(parent, leaf);
        int fd = openat(parent, leaf, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (fd < 0) return kWrite;
        std::vector<unsigned char> buf(1 << 16);
        uint64_t left = size;
        int rc = kOk;
        while (left > 0 && rc == kOk) {
            size_t want = left < buf.size() ? (size_t)left : buf.size();
            if (!in_.read_full(buf.data(), want)) { rc = kCorrupt; break; }
            for (size_t off = 0; off < want;) {
                ssize_t put = write(fd, buf.data() + off, want - off);
                if (put <= 0) { rc = kWrite; break; }
                off += (size_t)put;
            }
            left -= want;
        }
        if (rc == kOk) {
            fchmod(fd, mode);
            struct timespec times[2] = {{0, UTIME_OMIT}, mtime};
            futimens(fd, times);
        }
        if (close(fd) != 0 && rc == kOk) rc = kWrite;
        if (rc == kOk && !in_.skip(padded - size)) rc = kCorrupt;
        return rc;
    }

    // Directory modes and times last: a read-only directory would refuse its
    // own entries, and every entry written into it moves its mtime.
    int finish() {
        for (auto it = dirs_.rbegin(); it != dirs_.rend(); ++it) {
            std::vector<std::string> parts;
            if (!components(it->path, parts) || parts.empty()) continue;
            int parent = open_parent(root_, parts);
            if (parent < 0) continue;
            struct timespec times[2] = {{0, UTIME_OMIT}, it->mtime};
            utimensat(parent, parts.back().c_str(), times, AT_SYMLINK_NOFOLLOW);
            fchmodat(parent, parts.back().c_str(), it->mode | 0700, 0);   // we keep access to it
            close(parent);
        }
        return kOk;
    }
};

int open_target(const char* dst_dir) {
    std::string dir(dst_dir);
    for (size_t i = 1; i <= dir.size(); ++i)                // mkdir -p
        if (i == dir.size() || dir[i] == '/') mkdir(dir.substr(0, i).c_str(), 0755);
    return open(dst_dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
}

}  // namespace

extern "C" int32_t aiml_compress_untar(const char* src, const char* dst_dir) {
    if (!src || !dst_dir || !*src || !*dst_dir) return kArgs;
    std::unique_ptr<Input> in = open_any(src);
    if (!in) return kOpen;
    int root = open_target(dst_dir);
    if (root < 0) return kWrite;
    int rc = Extractor(*in, root).run();
    close(root);
    return rc;
}

extern "C" int32_t aiml_compress_unar(const char* src, const char* dst_dir) {
    if (!src || !dst_dir || !*src || !*dst_dir) return kArgs;
    FILE* f = fopen(src, "rbe");
    if (!f) return kOpen;
    FileInput in(f);
    unsigned char magic[8];
    if (!in.read_full(magic, 8) || memcmp(magic, "!<arch>\n", 8) != 0) return kCorrupt;
    int root = open_target(dst_dir);
    if (root < 0) return kWrite;
    std::string long_names;                                  // GNU "//" member
    int rc = kOk;
    unsigned char h[60];
    while (rc == kOk && in.read_full(h, 60)) {
        if (h[58] != '`' || h[59] != '\n') { rc = kCorrupt; break; }
        uint64_t size = strtoull(std::string((const char*)h + 48, 10).c_str(), nullptr, 10);   // decimal
        std::string name = text(h, 16);
        while (!name.empty() && name.back() == ' ') name.pop_back();
        uint64_t body = size;
        if (name.rfind("#1/", 0) == 0) {                      // BSD: the name opens the data
            size_t n = (size_t)strtoull(name.c_str() + 3, nullptr, 10);
            std::string real(n, '\0');
            if (n > body || !in.read_full((unsigned char*)&real[0], n)) { rc = kCorrupt; break; }
            body -= n;
            name = text((const unsigned char*)real.data(), n);
        } else if (name == "//") {                            // GNU long-name table
            long_names.assign((size_t)body, '\0');
            if (body && !in.read_full((unsigned char*)&long_names[0], (size_t)body)) { rc = kCorrupt; break; }
            if ((size & 1) && !in.skip(1)) rc = kCorrupt;
            continue;
        } else if (name == "/" || name == "/SYM64/") {        // symbol tables
            if (!in.skip(size + (size & 1))) rc = kCorrupt;
            continue;
        } else if (name.size() > 1 && name[0] == '/') {      // GNU "/123" → long name
            size_t at = (size_t)strtoull(name.c_str() + 1, nullptr, 10);
            if (at >= long_names.size()) { rc = kCorrupt; break; }
            size_t end = long_names.find('\n', at);
            name = long_names.substr(at, end == std::string::npos ? std::string::npos : end - at);
        }
        if (!name.empty() && name.back() == '/') name.pop_back();   // GNU terminator
        if (name.empty() || name == "." || name == ".." || name.find('/') != std::string::npos) { rc = kUnsafe; break; }
        clear_slot(root, name.c_str());
        int fd = openat(root, name.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
                        (mode_t)(strtoull(std::string((const char*)h + 40, 8).c_str(), nullptr, 8) & 0777));
        if (fd < 0) { rc = kWrite; break; }
        std::vector<unsigned char> buf(1 << 16);
        uint64_t left = body;
        while (left > 0 && rc == kOk) {
            size_t want = left < buf.size() ? (size_t)left : buf.size();
            if (!in.read_full(buf.data(), want)) { rc = kCorrupt; break; }
            if (write(fd, buf.data(), want) != (ssize_t)want) rc = kWrite;
            left -= want;
        }
        if (close(fd) != 0 && rc == kOk) rc = kWrite;
        if (rc == kOk && (size & 1) && !in.skip(1)) rc = kCorrupt;
    }
    close(root);
    return rc;
}
