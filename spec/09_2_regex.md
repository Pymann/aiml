# Chapter 9.2 — Regular Expressions (`regex`)

The `regex` namespace matches text against patterns.  It is what the compiler's
string-comparison lint ("consider a regular expression", spec/09 §str_cmp) points
to: one pattern instead of a chain of `eq` comparisons.

The engine is built into the runtime (no external library).  It runs in time
linear in the text for every pattern — there is no backtracking, so a pattern
from a configuration file or a user cannot hang a program.  The price is the same
as for RE2 / Go: no back-references and no look-around.

---

## 1. Functions

| Call | Returns | Meaning |
|------|---------|---------|
| `regex.match(pattern, text)` | `i32` | `1` if the **whole** text matches, else `0` (F09-2-1) |
| `regex.search(pattern, text)` | `i32` | byte index of the first match, `-1` if none (F09-2-2) |
| `regex.find(pattern, text)` | `list<string>` | the first match and its groups: `[whole, g1, g2, …]`; a group that did not take part is `""`; no match → empty list (F09-2-3) |
| `regex.find_all(pattern, text)` | `list<string>` | every non-overlapping match, left to right (whole matches) (F09-2-4) |
| `regex.replace(pattern, text, with)` | `string` | every match replaced; in `with`, `$0`…`$9` insert the match / a group, `$$` is a `$` (F09-2-5) |
| `regex.split(pattern, text)` | `list<string>` | the text between the matches (`n` matches → `n+1` parts) (F09-2-6) |
| `regex.error()` | `string` | why the last pattern on this thread did not compile; `""` if it did (F09-2-7) |

An invalid pattern never aborts: `match` gives `0`, `search` `-1`, `find`,
`find_all` an empty list, `replace` the text unchanged, `split` a one-element list
with the text — and `regex.error()` says what is wrong and where ("pattern
column 4: missing )").

Compiled patterns are cached per thread (the last 64), so a literal pattern in a
loop is compiled once.

```aiml
- decl: [ver, list<string>, { call: [regex.find, "^(\\d+)\\.(\\d+)(?:\\.(\\d+))?", "18.1.3-1ubuntu1"] }]
# ver = ["18.1.3", "18", "1", "3"]
- if: { condition: { eq: [{ call: [regex.match, "(?i)lib.*\\.so(\\.\\d+)*", name] }, 1] }, then: [...] }
- decl: [flat, string, { call: [regex.replace, "\\s+", "  a   b  ", " "] }]   # " a b "
```

## 2. Syntax (F09-2-8)

| Form | Matches |
|------|---------|
| `x` | the character x (UTF-8 text is matched by code point) |
| `.` | any character except `\n` (with flag `s`: any character) |
| `[abc]` `[a-z0-9_]` `[^…]` | a character class / its complement; `\d \w \s` and escapes work inside |
| `\d \D` | digit `[0-9]` / not |
| `\w \W` | word character `[A-Za-z0-9_]` / not |
| `\s \S` | white space `[ \t\n\r\f\v]` / not |
| `\b \B` | word boundary / not |
| `^ $` | start / end of the text (with flag `m`: of a line) |
| `\n \t \r \f \v \xHH \\ \. …` | escapes; any punctuation can be escaped |
| `(re)` | capturing group (numbered from 1, left to right) |
| `(?:re)` | group without capture |
| `a\|b` | alternation, leftmost alternative first |
| `* + ?` | 0+ / 1+ / 0–1, greedy |
| `{n} {n,} {n,m}` | counted repetition (n, m ≤ 1000) |
| `*? +? ?? {n,m}?` | the same, lazy (as few as possible) |
| `(?i) (?m) (?s)` | flags at the very start of the pattern: case-insensitive (ASCII), multi-line anchors, dot matches newline; may be combined: `(?im)` |

Semantics are leftmost-first like Perl, Python and Go: of the matches that start
earliest, the one the pattern's priorities (greedy / lazy, left alternative
first) prefer is taken.

Not supported (a compile error with `regex.error()`): back-references `\1`,
look-around `(?=…)` `(?!…)` `(?<=…)`, named groups, possessive quantifiers,
Unicode classes `\p{…}`.
