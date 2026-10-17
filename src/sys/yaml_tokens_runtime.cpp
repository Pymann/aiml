// yaml_tokens_runtime.cpp — spec/60 §... `yaml.tokens`, getrennt von
// yaml_runtime.cpp.
//
// Grund fuer die eigene Uebersetzungseinheit: der Lexer braucht **kein**
// yaml-cpp, der Rest des Namensraums schon.  Damit laesst sich genau dieser
// Teil auch fuer WASM bauen — und der musste es, weil die cosmos-Oberflaeche
// ihre Pipeline-Hervorhebung damit macht und der Client sonst mit
// `undefined symbol: aiml_yaml_tokens` nicht linkt (gemessen 2026-10-12).
#include "yaml_runtime.h"
#include "string_runtime.h"

#include <cstdint>
#include <string>

/* ── spec/60: yaml.tokens — fehlertoleranter Lexer fuer Hervorhebung ──────────
 *
 * Bewusst KEIN Parser: beim Tippen ist der Text fast immer ungueltig, und genau
 * dann wird die Hervorhebung gebraucht.  Ein Parser liefert dort gar nichts und
 * verwirft ausserdem Kommentare, die eingefaerbt werden muessen.
 *
 * Vertrag (spec/60): nie ein Fehlschlag, Byte-Offsets, ueberschneidungsfrei und
 * in Reihenfolge, zeilenlokal (kein Token ueberspannt ein \n), keine Semantik —
 * `key` heisst "Skalar vor einem ':'", nicht "gueltiger Mapping-Schluessel". */
namespace {
struct AimlI32ListRaw { int32_t size; int32_t capacity; int32_t* data; };

void tok_push(AimlI32ListRaw* l, int32_t v) {
    if (!l) return;
    if (l->size >= l->capacity) {
        int32_t cap = l->capacity > 0 ? l->capacity * 2 : 16;
        int32_t* nd = (int32_t*)realloc(l->data, (size_t)cap * sizeof(int32_t));
        if (!nd) return;
        l->data = nd; l->capacity = cap;
    }
    l->data[l->size++] = v;
}
void emit(AimlI32ListRaw* l, int32_t start, int32_t len, int32_t kind) {
    if (len <= 0) return;
    tok_push(l, start); tok_push(l, len); tok_push(l, kind);
}

enum YamlTokKind : int32_t {
    kTokText = 0, kTokComment = 1, kTokKey = 2, kTokString = 3, kTokNumber = 4,
    kTokKeyword = 5, kTokPunct = 6, kTokAnchor = 7, kTokTag = 8, kTokDirective = 9
};

bool is_punct(char c) {
    return c == '-' || c == ':' || c == ',' || c == '[' || c == ']' ||
           c == '{' || c == '}' || c == '?';
}
bool scalar_is_number(const std::string& s) {
    if (s.empty()) return false;
    size_t i = (s[0] == '-' || s[0] == '+') ? 1 : 0;
    if (i >= s.size()) return false;
    bool digit = false, dot = false;
    for (; i < s.size(); ++i) {
        if (s[i] >= '0' && s[i] <= '9') { digit = true; continue; }
        if (s[i] == '.' && !dot) { dot = true; continue; }
        return false;
    }
    return digit;
}
bool scalar_is_keyword(const std::string& s) {
    static const char* kw[] = {"true","false","null","~","yes","no","on","off",
                               "True","False","Null","TRUE","FALSE","NULL"};
    for (const char* k : kw) if (s == k) return true;
    return false;
}
} // namespace

extern "C" int32_t aiml_yaml_tokens(const char* text, void* out_list) {
    AimlI32ListRaw* out = static_cast<AimlI32ListRaw*>(out_list);
    if (!text || !out) return 0;
    const std::string src(text);
    const int32_t n = (int32_t)src.size();
    int32_t i = 0;
    while (i < n) {
        /* Eine Zeile am Stueck — damit kann kein Token ein \n ueberspannen. */
        int32_t line_end = i;
        while (line_end < n && src[(size_t)line_end] != '\n') ++line_end;
        int32_t p = i;
        bool key_done = false;   /* der erste Skalar der Zeile kann ein Schluessel sein */

        /* Direktiven und Dokumentmarker gelten fuer die ganze Zeile. */
        int32_t ws = p;
        while (ws < line_end && (src[(size_t)ws] == ' ' || src[(size_t)ws] == '\t')) ++ws;
        if (ws < line_end && (src[(size_t)ws] == '%' ||
                              src.compare((size_t)ws, 3, "---") == 0 ||
                              src.compare((size_t)ws, 3, "...") == 0)) {
            emit(out, ws, line_end - ws, kTokDirective);
            i = line_end + 1;
            continue;
        }

        while (p < line_end) {
            char c = src[(size_t)p];
            if (c == ' ' || c == '\t') { ++p; continue; }
            if (c == '#') { emit(out, p, line_end - p, kTokComment); p = line_end; break; }
            if (c == '\'' || c == '"') {
                char q = c; int32_t st = p; ++p;
                while (p < line_end) {
                    if (src[(size_t)p] == '\\' && q == '"') { p += 2; continue; }
                    if (src[(size_t)p] == q) { ++p; break; }
                    ++p;
                }
                if (p > line_end) p = line_end;   /* offener String endet an der Zeile */
                emit(out, st, p - st, kTokString);
                key_done = true;
                continue;
            }
            if (c == '&' || c == '*') {
                int32_t st = p; ++p;
                while (p < line_end && !is_punct(src[(size_t)p]) &&
                       src[(size_t)p] != ' ' && src[(size_t)p] != '\t') ++p;
                emit(out, st, p - st, kTokAnchor);
                key_done = true;
                continue;
            }
            if (c == '!') {
                int32_t st = p; ++p;
                while (p < line_end && src[(size_t)p] != ' ' && src[(size_t)p] != '\t' &&
                       !is_punct(src[(size_t)p])) ++p;
                emit(out, st, p - st, kTokTag);
                key_done = true;
                continue;
            }
            if (is_punct(c)) {
                /* '-' direkt vor einer Ziffer gehoert zur Zahl, nicht zum Strich. */
                bool neg = (c == '-' && p + 1 < line_end &&
                            src[(size_t)p+1] >= '0' && src[(size_t)p+1] <= '9');
                if (!neg) { emit(out, p, 1, kTokPunct); ++p; continue; }
            }
            /* Skalar bis zum naechsten Trenner. */
            int32_t st = p;
            while (p < line_end && !is_punct(src[(size_t)p]) && src[(size_t)p] != '#') ++p;
            int32_t e = p;
            while (e > st && (src[(size_t)e-1] == ' ' || src[(size_t)e-1] == '\t')) --e;
            if (e <= st) continue;
            std::string word = src.substr((size_t)st, (size_t)(e - st));
            int32_t kind = kTokText;
            if (!key_done && p < line_end && src[(size_t)p] == ':') kind = kTokKey;
            else if (scalar_is_number(word))  kind = kTokNumber;
            else if (scalar_is_keyword(word)) kind = kTokKeyword;
            emit(out, st, e - st, kind);
            key_done = true;
        }
        i = line_end + 1;
    }
    return out->size / 3;
}
