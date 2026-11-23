/* AIML — Vocab (i18n) Runtime — implementation.
 *
 * Design notes:
 *   • Module-level state: a map<string, vector<string>> for the
 *     translations + a vector<string> for the language codes.
 *     Exists once per process; hot-reloadable via aiml_vocab_load().
 *   • The returned const char* points into the internal std::string
 *     storage of the map — as long as no aiml_vocab_load() runs
 *     in between, the pointers are stable.
 *   • Hand-rolled JSON parser (no nlohmann): the same minimal
 *     vocabulary as prefab_runtime.cpp, saves a dependency.
 */

#include "vocab_runtime.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

/* ── State ──────────────────────────────────────────────────────────── */

std::vector<std::string>                                g_language_codes;
std::unordered_map<std::string, std::vector<std::string>> g_vocab;
int32_t                                                 g_current_language = 0;

/* ── Mini JSON parser (see prefab_runtime.cpp for the template) ──────── */

void skip_ws(const char*& p) {
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
        ++p;
}

/* Parses a JSON string literal — advances p past the closing quote,
 * returns the decoded UTF-8 content.  Supports \n, \t, \\, \", \uXXXX
 * (BMP only).  On error returns "" and does NOT advance p. */
std::string parse_string(const char*& p) {
    skip_ws(p);
    if (*p != '"') return std::string();
    ++p;
    std::string out;
    while (*p && *p != '"') {
        if (*p == '\\' && p[1]) {
            char c = p[1];
            switch (c) {
                case 'n':  out += '\n'; break;
                case 't':  out += '\t'; break;
                case 'r':  out += '\r'; break;
                case '\\': out += '\\'; break;
                case '"':  out += '"';  break;
                case '/':  out += '/';  break;
                case 'u': {
                    if (p[2] && p[3] && p[4] && p[5]) {
                        char hex[5] = { p[2], p[3], p[4], p[5], 0 };
                        unsigned code = (unsigned)std::strtoul(hex, nullptr, 16);
                        /* UTF-8 encode (BMP only, no surrogate pairs) */
                        if (code < 0x80) {
                            out += (char)code;
                        } else if (code < 0x800) {
                            out += (char)(0xC0 | (code >> 6));
                            out += (char)(0x80 | (code & 0x3F));
                        } else {
                            out += (char)(0xE0 | (code >> 12));
                            out += (char)(0x80 | ((code >> 6) & 0x3F));
                            out += (char)(0x80 | (code & 0x3F));
                        }
                        p += 4;
                    }
                    break;
                }
                default: out += c; break;
            }
            p += 2;
        } else {
            out += *p++;
        }
    }
    if (*p == '"') ++p;
    return out;
}

/* Consumes exactly one JSON value at *p, discarding it.  Used to skip
 * unknown object members without disturbing the parse.  Returns false on
 * malformed input. */
bool skip_value(const char*& p) {
    skip_ws(p);
    if (*p == '"') { (void)parse_string(p); return true; }
    if (*p == '{' || *p == '[') {
        char open = *p, close = (open == '{') ? '}' : ']';
        int depth = 0;
        do {
            if (*p == open)  ++depth;
            if (*p == close) --depth;
            ++p;
        } while (*p && depth > 0);
        return depth == 0;
    }
    /* number / true / false / null — skip until delimiter */
    while (*p && *p != ',' && *p != '}' && *p != ']') ++p;
    return true;
}

/* Parses a JSON array of strings at *p.  Fills `out`, returns false on
 * malformed input.  Non-string elements are stored as empty strings so
 * the index positions line up with the sibling list. */
bool parse_string_array(const char*& p, std::vector<std::string>& out) {
    skip_ws(p);
    if (*p != '[') return false;
    ++p;
    while (true) {
        skip_ws(p);
        if (*p == ']') { ++p; return true; }
        if (*p == '"') {
            out.push_back(parse_string(p));
        } else {
            /* tolerate non-string entry — insert empty slot to keep indices */
            out.emplace_back();
            skip_value(p);
        }
        skip_ws(p);
        if (*p == ',') { ++p; continue; }
        if (*p == ']') { ++p; return true; }
        return false;   /* malformed */
    }
}

}   /* anonymous namespace */

/* ── Public API ───────────────────────────────────────────────────────── */

extern "C"
int32_t aiml_vocab_load(const char* languages_json, const char* vocab_json) {
    if (!languages_json || !vocab_json) return -1;

    /* Reset state. */
    g_language_codes.clear();
    g_vocab.clear();
    /* Current-language stays — the caller may re-select after reload,
     * and if the new set has fewer languages we'll clamp on next set. */

    /* ── languages.json — expected {"0": "en", "1": "de", …}
     * We collect (index, code) pairs and sort by numeric index so the
     * order is stable regardless of JSON key iteration. */
    struct LangEntry { int32_t idx; std::string code; };
    std::vector<LangEntry> lang_entries;
    {
        const char* p = languages_json;
        skip_ws(p);
        if (*p != '{') return -1;
        ++p;
        while (true) {
            skip_ws(p);
            if (*p == '}') { ++p; break; }
            if (*p != '"') return -1;
            std::string key = parse_string(p);
            skip_ws(p);
            if (*p != ':') return -1;
            ++p;
            skip_ws(p);
            std::string val = parse_string(p);
            char* end = nullptr;
            long ki = std::strtol(key.c_str(), &end, 10);
            if (end == key.c_str()) continue;
            lang_entries.push_back({ (int32_t)ki, std::move(val) });
            skip_ws(p);
            if (*p == ',') { ++p; continue; }
            if (*p == '}') { ++p; break; }
            return -1;
        }
    }
    /* Sort by index — insertion sort is fine at 5-language scale. */
    for (size_t i = 1; i < lang_entries.size(); ++i) {
        for (size_t j = i;
             j > 0 && lang_entries[j].idx < lang_entries[j - 1].idx;
             --j) {
            std::swap(lang_entries[j], lang_entries[j - 1]);
        }
    }
    g_language_codes.reserve(lang_entries.size());
    for (auto& le : lang_entries) g_language_codes.push_back(std::move(le.code));

    /* Clamp current_language into new range. */
    if (g_current_language >= (int32_t)g_language_codes.size())
        g_current_language = 0;
    if (g_current_language < 0) g_current_language = 0;

    /* ── vocab.json — expected {"source": ["src", "tr1", "tr2", …], …}
     * Value is a JSON array of strings; entry i maps to language index i. */
    {
        const char* p = vocab_json;
        skip_ws(p);
        if (*p != '{') return -2;
        ++p;
        while (true) {
            skip_ws(p);
            if (*p == '}') { ++p; break; }
            if (*p != '"') return -2;
            std::string key = parse_string(p);
            skip_ws(p);
            if (*p != ':') return -2;
            ++p;
            std::vector<std::string> arr;
            if (!parse_string_array(p, arr)) return -2;
            g_vocab.emplace(std::move(key), std::move(arr));
            skip_ws(p);
            if (*p == ',') { ++p; continue; }
            if (*p == '}') { ++p; break; }
            return -2;
        }
    }

    return 0;
}

extern "C"
void aiml_vocab_set_language(int32_t idx) {
    if (g_language_codes.empty()) return;
    if (idx < 0 || idx >= (int32_t)g_language_codes.size()) idx = 0;
    g_current_language = idx;
}

extern "C"
int32_t aiml_vocab_current_language(void) {
    return g_current_language;
}

extern "C"
int32_t aiml_vocab_language_count(void) {
    return (int32_t)g_language_codes.size();
}

extern "C"
const char* aiml_vocab_language_code(int32_t idx) {
    if (idx < 0 || idx >= (int32_t)g_language_codes.size()) return nullptr;
    return g_language_codes[(size_t)idx].c_str();
}

extern "C"
const char* aiml_vocab_lookup(const char* source) {
    if (!source) return "";
    /* Fast paths: no vocab loaded OR current language is the source
     * (index 0) → pass through without map lookup. */
    if (g_vocab.empty() || g_current_language <= 0) return source;
    auto it = g_vocab.find(source);
    if (it == g_vocab.end()) return source;
    const auto& translations = it->second;
    if ((int32_t)translations.size() <= g_current_language) return source;
    const std::string& tr = translations[(size_t)g_current_language];
    /* Empty translation slot → keep source (opt-in mask for "not yet
     * translated"). */
    if (tr.empty()) return source;
    return tr.c_str();
}
