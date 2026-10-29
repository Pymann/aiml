// regex namespace (spec/09_2): a Pike-VM regular expression engine.
//
// Patterns compile to a small instruction program that is run breadth-first
// over the text (Thompson / Pike), so every match costs O(text × program) —
// no backtracking, no pathological patterns.  Leftmost-first semantics (Perl,
// Python, Go): thread priority decides between matches that start at the same
// place.  The text is matched by UTF-8 code point.

#include "regex_runtime.h"
#include "string_runtime.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr uint32_t kMaxCodePoint = 0x10FFFF;
constexpr size_t   kMaxProgram   = 20000;   // instructions after expanding {n,m}
constexpr int      kMaxRepeat    = 1000;
constexpr size_t   kCacheSize    = 64;

/* ── program ─────────────────────────────────────────────────────────── */

enum class Op : uint8_t { Char, Any, AnyNL, Class, Match, Jmp, Split, Save, Bol, Eol, WordB, NotWordB };

struct Inst {
    Op       op = Op::Match;
    uint32_t ch = 0;      // Char: code point (folded when case-insensitive)
    int      x = 0;       // Jmp/Split: target (Split: preferred); Save: slot; Class: index
    int      y = 0;       // Split: the other target
};

struct CharClass {
    std::vector<std::pair<uint32_t, uint32_t>> ranges;
    bool negate = false;
    bool has(uint32_t c) const {
        bool in = false;
        for (const auto& r : ranges) if (c >= r.first && c <= r.second) { in = true; break; }
        return in != negate;
    }
};

struct Program {
    std::vector<Inst>      code;
    std::vector<CharClass> classes;
    int  groups = 1;           // including group 0 (the whole match)
    bool icase = false, multiline = false, dotall = false;
};

uint32_t fold(uint32_t c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
bool is_word(unsigned char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }

// The code point at `pos` and its byte length (an invalid byte is itself, length 1).
uint32_t decode(const char* s, size_t len, size_t pos, size_t& n) {
    unsigned char b = (unsigned char)s[pos];
    n = 1;
    if (b < 0x80) return b;
    int extra = (b >= 0xF8) ? 0 : (b >= 0xF0) ? 3 : (b >= 0xE0) ? 2 : (b >= 0xC0) ? 1 : 0;
    if (extra == 0) return b;                 // a stray continuation byte
    uint32_t cp = b & (0x3F >> extra);
    for (int i = 1; i <= extra; ++i) {
        if (pos + i >= len) return b;
        unsigned char nb = (unsigned char)s[pos + i];
        if ((nb & 0xC0) != 0x80) return b;
        cp = (cp << 6) | (nb & 0x3F);
    }
    n = (size_t)extra + 1;
    return cp;
}

/* ── parser: pattern → AST ───────────────────────────────────────────── */

struct Node {
    enum Kind { Lit, Dot, Cls, Bol, Eol, WordB, NotWordB, Group, Concat, Alt, Repeat, Empty } kind = Empty;
    uint32_t ch = 0;
    int      cls = -1;
    int      capture = -1;        // Group: capture index, -1 = none
    int      min = 0, max = 0;    // Repeat: max -1 = unbounded
    bool     greedy = true;
    std::vector<std::unique_ptr<Node>> kids;
};
using NodeP = std::unique_ptr<Node>;

NodeP make(Node::Kind k) { NodeP n(new Node); n->kind = k; return n; }

class Parser {
public:
    Parser(const std::string& pattern, Program& prog) : p_(pattern), prog_(prog) {}

    NodeP parse(std::string& error) {
        read_flags();
        NodeP root = alternation();
        if (error_.empty() && pos_ < p_.size()) fail(p_[pos_] == ')' ? "unbalanced )" : "unexpected character");
        error = error_;
        return root;
    }

private:
    const std::string& p_;
    Program& prog_;
    size_t pos_ = 0;
    std::string error_;

    bool done() const { return pos_ >= p_.size() || !error_.empty(); }
    void fail(const std::string& why) {
        if (error_.empty()) error_ = "pattern column " + std::to_string(pos_ + 1) + ": " + why;
    }

    void read_flags() {                        // (?ims) at the very start
        if (p_.compare(0, 2, "(?") != 0) return;
        size_t i = 2;
        while (i < p_.size() && (p_[i] == 'i' || p_[i] == 'm' || p_[i] == 's')) ++i;
        if (i == 2 || i >= p_.size() || p_[i] != ')') return;
        for (size_t k = 2; k < i; ++k) {
            if (p_[k] == 'i') prog_.icase = true;
            if (p_[k] == 'm') prog_.multiline = true;
            if (p_[k] == 's') prog_.dotall = true;
        }
        pos_ = i + 1;
    }

    NodeP alternation() {
        NodeP first = concatenation();
        if (pos_ >= p_.size() || p_[pos_] != '|') return first;
        NodeP alt = make(Node::Alt);
        alt->kids.push_back(std::move(first));
        while (!done() && p_[pos_] == '|') {
            ++pos_;
            alt->kids.push_back(concatenation());
        }
        return alt;
    }

    NodeP concatenation() {
        NodeP cat = make(Node::Concat);
        while (!done() && p_[pos_] != '|' && p_[pos_] != ')') cat->kids.push_back(repetition());
        return cat;
    }

    // {n}, {n,}, {n,m} at pos_; false (pos_ unchanged) when it is not one.
    bool counted(int& lo, int& hi) {
        size_t i = pos_ + 1;
        auto number = [&](int& out) {
            size_t start = i;
            long v = 0;
            while (i < p_.size() && p_[i] >= '0' && p_[i] <= '9' && v <= kMaxRepeat * 10L) v = v * 10 + (p_[i++] - '0');
            out = (int)v;
            return i > start;
        };
        if (!number(lo)) return false;
        hi = lo;
        if (i < p_.size() && p_[i] == ',') {
            ++i;
            if (!number(hi)) hi = -1;
        }
        if (i >= p_.size() || p_[i] != '}') return false;
        pos_ = i + 1;
        return true;
    }

    NodeP repetition() {
        NodeP atom_node = atom();
        if (done()) return atom_node;
        int lo = 0, hi = 0;
        char q = p_[pos_];
        if (q == '*')      { lo = 0; hi = -1; ++pos_; }
        else if (q == '+') { lo = 1; hi = -1; ++pos_; }
        else if (q == '?') { lo = 0; hi = 1;  ++pos_; }
        else if (q == '{') {
            if (!counted(lo, hi)) return atom_node;          // a literal '{' follows
            if (lo > kMaxRepeat || hi > kMaxRepeat) { fail("repetition count above 1000"); return atom_node; }
            if (hi != -1 && hi < lo) { fail("repetition {n,m} with m < n"); return atom_node; }
        } else return atom_node;
        NodeP rep = make(Node::Repeat);
        rep->min = lo;
        rep->max = hi;
        if (pos_ < p_.size() && p_[pos_] == '?') { rep->greedy = false; ++pos_; }
        if (pos_ < p_.size() && (p_[pos_] == '*' || p_[pos_] == '+' || p_[pos_] == '?')) {
            fail("multiple repeat");
            return rep;
        }
        if (atom_node->kind == Node::Bol || atom_node->kind == Node::Eol
            || atom_node->kind == Node::WordB || atom_node->kind == Node::NotWordB) {
            fail("nothing to repeat");
        }
        rep->kids.push_back(std::move(atom_node));
        return rep;
    }

    uint32_t literal_cp() {
        size_t n = 1;
        uint32_t cp = decode(p_.data(), p_.size(), pos_, n);
        pos_ += n;
        return cp;
    }

    int add_class(CharClass cc) {
        prog_.classes.push_back(std::move(cc));
        return (int)prog_.classes.size() - 1;
    }

    static void add_shorthand(CharClass& cc, char kind) {
        auto& r = cc.ranges;
        switch (kind) {
            case 'd': r.push_back({'0', '9'}); break;
            case 'w': r.push_back({'0', '9'}); r.push_back({'A', 'Z'}); r.push_back({'a', 'z'}); r.push_back({'_', '_'}); break;
            case 's': r.push_back({' ', ' '}); r.push_back({'\t', '\r'}); break;   // \t \n \v \f \r
        }
    }

    // The complement of the ranges of a shorthand (for \D \W \S inside [...]).
    static void add_complement(CharClass& cc, char kind) {
        CharClass pos_set;
        add_shorthand(pos_set, kind);
        std::vector<std::pair<uint32_t, uint32_t>> rs = pos_set.ranges;
        std::sort(rs.begin(), rs.end());
        uint32_t next = 0;
        for (const auto& r : rs) {
            if (r.first > next) cc.ranges.push_back({next, r.first - 1});
            next = r.second + 1;
        }
        if (next <= kMaxCodePoint) cc.ranges.push_back({next, kMaxCodePoint});
    }

    // An escape at pos_ (after the backslash).  Returns: 'c' literal (cp set),
    // a shorthand letter (d D w W s S), or 'b'/'B'; 0 on error.
    char escape(uint32_t& cp) {
        const size_t backslash = pos_ - 1;   // errors point at the backslash
        auto bad = [&](const std::string& why) { pos_ = backslash; fail(why); return (char)0; };
        if (pos_ >= p_.size()) return bad("trailing backslash");
        char e = p_[pos_++];
        switch (e) {
            case 'd': case 'D': case 'w': case 'W': case 's': case 'S': case 'b': case 'B': return e;
            case 'n': cp = '\n'; return 'c';
            case 't': cp = '\t'; return 'c';
            case 'r': cp = '\r'; return 'c';
            case 'f': cp = '\f'; return 'c';
            case 'v': cp = '\v'; return 'c';
            case '0': cp = 0;    return 'c';
            case 'x': {
                auto hex = [](char h) -> int {
                    if (h >= '0' && h <= '9') return h - '0';
                    if (h >= 'a' && h <= 'f') return h - 'a' + 10;
                    if (h >= 'A' && h <= 'F') return h - 'A' + 10;
                    return -1;
                };
                if (pos_ + 1 >= p_.size() || hex(p_[pos_]) < 0 || hex(p_[pos_ + 1]) < 0) {
                    return bad("\\x needs two hex digits");
                }
                cp = (uint32_t)(hex(p_[pos_]) * 16 + hex(p_[pos_ + 1]));
                pos_ += 2;
                return 'c';
            }
            default: break;
        }
        if (e >= '1' && e <= '9') return bad("back-references are not supported");
        if ((e >= 'a' && e <= 'z') || (e >= 'A' && e <= 'Z')) return bad(std::string("unknown escape \\") + e);
        --pos_;
        cp = literal_cp();
        return 'c';
    }

    NodeP char_class() {                       // pos_ is after '['
        CharClass cc;
        if (pos_ < p_.size() && p_[pos_] == '^') { cc.negate = true; ++pos_; }
        bool first = true;
        while (true) {
            if (pos_ >= p_.size()) { fail("missing ]"); break; }
            if (p_[pos_] == ']' && !first) { ++pos_; break; }
            first = false;
            uint32_t lo = 0;
            if (p_[pos_] == '\\') {
                ++pos_;
                char kind = escape(lo);
                if (!kind) break;
                if (kind == 'b' || kind == 'B') { fail("\\b inside a class"); break; }
                if (kind != 'c') {
                    char base = (char)(kind | 0x20);
                    if (kind == base) add_shorthand(cc, base); else add_complement(cc, base);
                    continue;
                }
            } else {
                lo = literal_cp();
            }
            uint32_t hi = lo;
            if (pos_ + 1 < p_.size() && p_[pos_] == '-' && p_[pos_ + 1] != ']') {
                ++pos_;
                if (p_[pos_] == '\\') {
                    ++pos_;
                    char kind = escape(hi);
                    if (kind != 'c') { if (kind) fail("a class shorthand cannot end a range"); break; }
                } else {
                    hi = literal_cp();
                }
                if (hi < lo) { fail("range out of order"); break; }
            }
            cc.ranges.push_back({lo, hi});
        }
        NodeP n = make(Node::Cls);
        n->cls = add_class(std::move(cc));
        return n;
    }

    NodeP atom() {
        char c = p_[pos_];
        switch (c) {
            case '(': {
                ++pos_;
                NodeP g = make(Node::Group);
                if (p_.compare(pos_, 2, "?:") == 0) pos_ += 2;
                else if (pos_ < p_.size() && p_[pos_] == '?') {
                    fail("only (?:…) groups are supported (flags go at the start)");
                    return g;
                } else g->capture = prog_.groups++;
                g->kids.push_back(alternation());
                if (pos_ >= p_.size() || p_[pos_] != ')') { fail("missing )"); return g; }
                ++pos_;
                return g;
            }
            case '[': ++pos_; return char_class();
            case '.': ++pos_; return make(Node::Dot);
            case '^': ++pos_; return make(Node::Bol);
            case '$': ++pos_; return make(Node::Eol);
            case '*': case '+': case '?': fail("nothing to repeat"); ++pos_; return make(Node::Empty);
            case '\\': {
                ++pos_;
                uint32_t cp = 0;
                char kind = escape(cp);
                if (!kind) return make(Node::Empty);
                if (kind == 'b') return make(Node::WordB);
                if (kind == 'B') return make(Node::NotWordB);
                if (kind != 'c') {
                    CharClass cc;
                    char base = (char)(kind | 0x20);
                    add_shorthand(cc, base);
                    cc.negate = kind != base;
                    NodeP n = make(Node::Cls);
                    n->cls = add_class(std::move(cc));
                    return n;
                }
                NodeP n = make(Node::Lit);
                n->ch = cp;
                return n;
            }
            default: {
                NodeP n = make(Node::Lit);
                n->ch = literal_cp();
                return n;
            }
        }
    }
};

/* ── compiler: AST → program ─────────────────────────────────────────── */

class Emitter {
public:
    explicit Emitter(Program& prog) : prog_(prog) {}

    bool emit_all(const Node& root, std::string& error) {
        add(Op::Save, 0);
        node(root);
        add(Op::Save, 1);
        add(Op::Match);
        if (too_big_) error = "pattern too large (repetitions expand beyond " + std::to_string(kMaxProgram) + " steps)";
        return !too_big_;
    }

private:
    Program& prog_;
    bool too_big_ = false;
    Inst spare_;                     // write target once the program is too big

    int add(Op op, int x = 0, uint32_t ch = 0) {
        if (prog_.code.size() >= kMaxProgram) { too_big_ = true; return -1; }
        Inst in;
        in.op = op; in.x = x; in.ch = ch;
        prog_.code.push_back(in);
        return (int)prog_.code.size() - 1;
    }
    int here() const { return (int)prog_.code.size(); }
    Inst& at(int index) { return (index >= 0 && !too_big_) ? prog_.code[(size_t)index] : spare_; }

    // A split at `index` between `body` and `exit`; greedy prefers the body.
    void branch(int index, int body, int exit, bool greedy) {
        at(index).x = greedy ? body : exit;
        at(index).y = greedy ? exit : body;
    }

    void node(const Node& n) {
        if (too_big_) return;
        switch (n.kind) {
            case Node::Empty: break;
            case Node::Lit:   add(Op::Char, 0, prog_.icase ? fold(n.ch) : n.ch); break;
            case Node::Dot:   add(prog_.dotall ? Op::Any : Op::AnyNL); break;
            case Node::Cls:   add(Op::Class, n.cls); break;
            case Node::Bol:   add(Op::Bol); break;
            case Node::Eol:   add(Op::Eol); break;
            case Node::WordB: add(Op::WordB); break;
            case Node::NotWordB: add(Op::NotWordB); break;
            case Node::Concat: for (const auto& k : n.kids) node(*k); break;
            case Node::Group:
                if (n.capture >= 0) add(Op::Save, 2 * n.capture);
                node(*n.kids[0]);
                if (n.capture >= 0) add(Op::Save, 2 * n.capture + 1);
                break;
            case Node::Alt: {
                // split(a, next); a; jmp end; next: split(b, next2); b; jmp end; … last
                std::vector<int> exits;
                for (size_t i = 0; i + 1 < n.kids.size(); ++i) {
                    int split = add(Op::Split);
                    int body = here();
                    node(*n.kids[i]);
                    exits.push_back(add(Op::Jmp));
                    branch(split, body, here(), true);
                }
                node(*n.kids.back());
                for (int j : exits) at(j).x = here();
                break;
            }
            case Node::Repeat: repeat(n); break;
        }
    }

    void repeat(const Node& n) {
        const Node& body = *n.kids[0];
        for (int i = 0; i < n.min; ++i) node(body);
        if (n.max == -1) {                       // loop: split(body, exit); body; jmp split
            int split = add(Op::Split);
            int start = here();
            node(body);
            add(Op::Jmp, split);
            branch(split, start, here(), n.greedy);
            return;
        }
        std::vector<std::pair<int, int>> optional;   // (split, body start) per optional copy
        for (int i = n.min; i < n.max; ++i) {
            int split = add(Op::Split);
            optional.push_back({split, here()});
            node(body);
        }
        for (const auto& o : optional) branch(o.first, o.second, here(), n.greedy);
    }
};

/* ── the Pike VM ─────────────────────────────────────────────────────── */

struct Thread {
    int pc;
    std::vector<int> caps;
};

class Vm {
public:
    Vm(const Program& prog, const char* text, size_t len)
        : prog_(prog), text_(text), len_(len), mark_(prog.code.size(), (size_t)-1) {}

    // The leftmost-first match starting at or after `from` (anchored: exactly at
    // `from`, and only one that ends at the end of the text).  caps[0..1] = span.
    bool run(size_t from, bool whole, std::vector<int>& out) {
        std::vector<Thread> clist, nlist;
        size_t& gen = ++gen_;                // fresh per run: marks of an earlier run never count
        bool matched = false;
        std::vector<int> empty((size_t)prog_.groups * 2, -1);
        size_t pos = from;
        while (true) {
            if (!matched && (!whole || pos == from)) add(clist, gen, 0, empty, pos);
            if (clist.empty()) {                // nothing alive: try the next start, if any
                if (matched || whole || pos >= len_) break;
                size_t skip = 1;
                decode(text_, len_, pos, skip);
                pos += skip;
                ++gen;                          // a new position: earlier marks do not apply
                continue;
            }
            size_t n = 0;
            uint32_t cp = pos < len_ ? decode(text_, len_, pos, n) : 0;
            bool at_end = pos >= len_;
            ++gen;
            nlist.clear();
            for (size_t i = 0; i < clist.size(); ++i) {
                const Thread& th = clist[i];
                const Inst& in = prog_.code[(size_t)th.pc];
                bool step = false;
                switch (in.op) {
                    case Op::Match:
                        if (whole && !at_end) break;
                        matched = true;
                        out = th.caps;
                        i = clist.size();             // lower-priority threads lose
                        break;
                    case Op::Char:  step = !at_end && (prog_.icase ? fold(cp) : cp) == in.ch; break;
                    case Op::Any:   step = !at_end; break;
                    case Op::AnyNL: step = !at_end && cp != '\n'; break;
                    case Op::Class: {
                        if (at_end) break;
                        const CharClass& cc = prog_.classes[(size_t)in.x];
                        step = cc.has(cp);
                        if (!step && prog_.icase) {
                            uint32_t lower = fold(cp);
                            uint32_t upper = (cp >= 'a' && cp <= 'z') ? cp - 32 : cp;
                            step = cc.has(lower) || cc.has(upper);
                        }
                        break;
                    }
                    default: break;
                }
                if (step) add(nlist, gen, th.pc + 1, th.caps, pos + n);
            }
            if (at_end) break;
            std::swap(clist, nlist);
            pos += n;
        }
        return matched;
    }

private:
    const Program& prog_;
    const char* text_;
    size_t len_;
    std::vector<size_t> mark_;            // generation in which a pc was added
    size_t gen_ = 0;

    bool assertion(Op op, size_t pos) const {
        switch (op) {
            case Op::Bol: return pos == 0 || (prog_.multiline && text_[pos - 1] == '\n');
            case Op::Eol: return pos >= len_ || (prog_.multiline && text_[pos] == '\n');
            case Op::WordB: case Op::NotWordB: {
                bool before = pos > 0 && is_word((unsigned char)text_[pos - 1]);
                bool after = pos < len_ && is_word((unsigned char)text_[pos]);
                return (before != after) == (op == Op::WordB);
            }
            default: return false;
        }
    }

    // Follow the empty transitions from `pc` in priority order; each consuming
    // instruction (or Match) reached becomes a thread of `list`.
    void add(std::vector<Thread>& list, size_t gen, int pc, const std::vector<int>& caps, size_t pos) {
        std::vector<Thread> stack;
        stack.push_back(Thread{pc, caps});
        while (!stack.empty()) {
            Thread th = std::move(stack.back());
            stack.pop_back();
            if (mark_[(size_t)th.pc] == gen) continue;
            mark_[(size_t)th.pc] = gen;
            const Inst& in = prog_.code[(size_t)th.pc];
            switch (in.op) {
                case Op::Jmp:
                    stack.push_back(Thread{in.x, std::move(th.caps)});
                    break;
                case Op::Split:
                    stack.push_back(Thread{in.y, th.caps});
                    stack.push_back(Thread{in.x, std::move(th.caps)});
                    break;
                case Op::Save:
                    th.caps[(size_t)in.x] = (int)pos;
                    stack.push_back(Thread{th.pc + 1, std::move(th.caps)});
                    break;
                case Op::Bol: case Op::Eol: case Op::WordB: case Op::NotWordB:
                    if (assertion(in.op, pos)) stack.push_back(Thread{th.pc + 1, std::move(th.caps)});
                    break;
                default:
                    list.push_back(std::move(th));
                    break;
            }
        }
    }
};

/* ── compiled-pattern cache (per thread) ────────────────────────────── */

thread_local std::string g_regex_error;

struct Compiled {
    std::string pattern;
    std::shared_ptr<Program> prog;     // null = the pattern does not compile
    std::string error;
};
thread_local std::vector<Compiled> g_cache;   // most recently used first

std::shared_ptr<Program> compile(const char* pattern) {
    std::string pat = pattern ? pattern : "";
    for (size_t i = 0; i < g_cache.size(); ++i) {
        if (g_cache[i].pattern != pat) continue;
        if (i > 0) std::rotate(g_cache.begin(), g_cache.begin() + (long)i, g_cache.begin() + (long)i + 1);
        g_regex_error = g_cache[0].error;
        return g_cache[0].prog;
    }
    auto prog = std::make_shared<Program>();
    std::string error;
    NodeP root = Parser(pat, *prog).parse(error);
    if (error.empty()) Emitter(*prog).emit_all(*root, error);
    Compiled entry{pat, error.empty() ? prog : nullptr, error};
    g_cache.insert(g_cache.begin(), std::move(entry));
    if (g_cache.size() > kCacheSize) g_cache.pop_back();
    g_regex_error = error;
    return g_cache[0].prog;
}

// Successive non-overlapping matches (an empty match right after a match is
// skipped, as in Go).  `each(caps)` returns false to stop.
template <typename F>
void each_match(const Program& prog, const char* text, size_t len, F each) {
    Vm vm(prog, text, len);
    size_t from = 0;
    int last_end = -1;
    std::vector<int> caps;
    while (from <= len) {
        if (!vm.run(from, false, caps)) break;
        int s = caps[0], e = caps[1];
        if (s == e && s == last_end) {                     // empty, abutting the previous match
            size_t n = 1;
            if ((size_t)s >= len) break;
            decode(text, len, (size_t)s, n);
            from = (size_t)s + n;
            continue;
        }
        if (!each(caps)) break;
        last_end = e;
        if (e > s) from = (size_t)e;
        else {
            if ((size_t)e >= len) break;
            size_t n = 1;
            decode(text, len, (size_t)e, n);
            from = (size_t)e + n;
        }
    }
}

std::string group_text(const char* text, const std::vector<int>& caps, int g) {
    if ((size_t)(2 * g + 1) >= caps.size() || caps[(size_t)2 * g] < 0 || caps[(size_t)2 * g + 1] < 0) return "";
    return std::string(text + caps[(size_t)2 * g], (size_t)(caps[(size_t)2 * g + 1] - caps[(size_t)2 * g]));
}

}  // namespace

/* ── C ABI (spec/09_2 §1) ────────────────────────────────────────────── */

extern "C" int32_t aiml_regex_match(const char* pattern, const char* text) {
    auto prog = compile(pattern);
    if (!prog) return 0;
    const char* t = text ? text : "";
    std::vector<int> caps;
    return Vm(*prog, t, strlen(t)).run(0, true, caps) ? 1 : 0;
}

extern "C" int32_t aiml_regex_search(const char* pattern, const char* text) {
    auto prog = compile(pattern);
    if (!prog) return -1;
    const char* t = text ? text : "";
    std::vector<int> caps;
    return Vm(*prog, t, strlen(t)).run(0, false, caps) ? caps[0] : -1;
}

extern "C" int32_t aiml_regex_find_fill(const char* pattern, const char* text, void* list) {
    auto prog = compile(pattern);
    if (!prog) return 0;
    const char* t = text ? text : "";
    std::vector<int> caps;
    if (!Vm(*prog, t, strlen(t)).run(0, false, caps)) return 0;
    for (int g = 0; g < prog->groups; ++g) {
        std::string part = group_text(t, caps, g);
        aiml_list_push_str_copy(list, part.data(), part.size());
    }
    return prog->groups;
}

extern "C" int32_t aiml_regex_find_all_fill(const char* pattern, const char* text, void* list) {
    auto prog = compile(pattern);
    if (!prog) return 0;
    const char* t = text ? text : "";
    int32_t count = 0;
    each_match(*prog, t, strlen(t), [&](const std::vector<int>& caps) {
        aiml_list_push_str_copy(list, t + caps[0], (size_t)(caps[1] - caps[0]));
        ++count;
        return true;
    });
    return count;
}

extern "C" char* aiml_regex_replace(const char* pattern, const char* text, const char* with) {
    const char* t = text ? text : "";
    auto prog = compile(pattern);
    if (!prog) return aiml_str_dup(t);
    const char* w = with ? with : "";
    std::string out;
    size_t copied = 0;
    each_match(*prog, t, strlen(t), [&](const std::vector<int>& caps) {
        out.append(t + copied, (size_t)caps[0] - copied);
        for (const char* r = w; *r; ++r) {
            if (*r == '$' && r[1] == '$') { out += '$'; ++r; continue; }
            if (*r == '$' && r[1] >= '0' && r[1] <= '9') { out += group_text(t, caps, r[1] - '0'); ++r; continue; }
            out += *r;
        }
        copied = (size_t)caps[1];
        return true;
    });
    out.append(t + copied);
    return aiml_str_ndup(out.data(), out.size());
}

extern "C" int32_t aiml_regex_split_fill(const char* pattern, const char* text, void* list) {
    const char* t = text ? text : "";
    size_t len = strlen(t);
    auto prog = compile(pattern);
    if (!prog) { aiml_list_push_str_copy(list, t, len); return 1; }
    int32_t count = 0;
    size_t start = 0;
    each_match(*prog, t, len, [&](const std::vector<int>& caps) {
        aiml_list_push_str_copy(list, t + start, (size_t)caps[0] - start);
        ++count;
        start = (size_t)caps[1];
        return true;
    });
    aiml_list_push_str_copy(list, t + start, len - start);
    return count + 1;
}

extern "C" char* aiml_regex_error(void) {
    return aiml_str_dup(g_regex_error.c_str());
}
