# Strings

`string` is an alias for `ptr<i8>` — a null-terminated UTF-8 C-string on the heap. Strings are
immutable by convention; methods that transform them return new allocations.

## Overview

```yaml
- decl: [greeting, string, "Hello, World!"]
- print: [greeting]
# Output: Hello, World!

# String interpolation: embed variables or expressions
- decl: [name, string, "Alice"]
- decl: [age,  i32,    30]
- print: ["${name} is ${age} years old."]
# Output: Alice is 30 years old.

# See 09_1_string_expansions.md for full interpolation details.
```

## Methods

All string methods require `call:`; the queries `.len`, `.empty` and `.byte_len` are properties
and are never called (spec/71 §2).

### `string.len` → `i32`

The number of **bytes** of the string's UTF-8 encoding — the unit every string position uses
(`substr`, `index_of` count bytes).  `"grün"` has `.len` 5.  A null string has `.len` 0.
`.byte_len` is the same number; `.empty` is `1` for `""` and for a null string.

```yaml
- decl: [s, string, "Hello"]
- decl: [n, i32, s.len]         # 5
```

> `sys.size(s)` is the size of the `string` **type** (a pointer), not of the text (spec/71 §1.1).

---

### `string.substr(start)` → `string`

Returns a new string from byte offset `start` to the end. Allocates via `strdup`.

```yaml
- decl: [s,   string, "Bearer token123"]
- decl: [tok, string]
- set:  [tok, { call: [s.substr, 7] }]
# tok == "token123"
```

### `string.substr(start, len)` → `string`

Returns a new string of at most `len` bytes starting at `start`. Allocates via `strndup`.

```yaml
- decl: [part, string]
- set:  [part, { call: [s.substr, 0, 6] }]
# part == "Bearer"
```

---

### `string.starts_with(prefix)` → `i32`

Returns `1` if the string begins with `prefix`, `0` otherwise. Implemented via `strncmp`.

```yaml
- decl: [ok, i32]
- set:  [ok, { call: [s.starts_with, "Bearer "] }]
# ok == 1
```

---

### `string.ends_with(suffix)` → `i32`

Returns `1` if the string ends with `suffix`, `0` otherwise. Returns `0` if the suffix is longer than the string. Empty suffix always matches.

```yaml
- decl: [path, string, "data/train.json"]
- decl: [is_json, i32, { call: [path.ends_with, ".json"] }]
# is_json == 1
- decl: [is_md, i32, { call: [path.ends_with, ".md"] }]
# is_md == 0
```

---

### `string.contains(needle)` → `i32`

`1` if `needle` occurs in the string (an empty needle always does), else `0` (F09-30).

```yaml
- if: { condition: { eq: [{ call: [flags.contains, "-O2"] }, 1] }, then: [...] }
```

### `string.index_of(needle)` → `i32`

Returns the byte offset of the **first** occurrence of `needle`, or `-1` if not found.
Implemented via `strstr`.

```yaml
- decl: [idx, i32]
- set:  [idx, { call: [s.index_of, "token"] }]
# idx == 7

- decl: [miss, i32]
- set:  [miss, { call: [s.index_of, "xyz"] }]
# miss == -1
```

---

### `string.lower()` → `string`

Returns a new string where all ASCII uppercase letters `A-Z` are converted to lowercase.
Characters outside ASCII uppercase remain unchanged.

```yaml
- decl: [s, string, "HeLLo AIML"]
- decl: [lowered, string]
- set:  [lowered, { call: [s.lower] }]
# lowered == "hello aiml"
```

---

### `string.upper()` → `string`

Returns a new string where all ASCII lowercase letters `a-z` are converted to uppercase.
Characters outside ASCII lowercase remain unchanged.

```yaml
- decl: [s, string, "HeLLo AIML"]
- decl: [upped, string]
- set:  [upped, { call: [s.upper] }]
# upped == "HELLO AIML"
```

---

### `string.title()` → `string`

Python-style title case. Uppercases the first character of every
whitespace-separated word, lowercases the rest. Punctuation — including
`_` and `-` — is **not** a word break; it stays in place and only ASCII
character casing changes.

For snake_case / kebab-case → display labels, chain with `replace`:

```yaml
- decl: [snake,   string, "smith_touch_master"]
- decl: [spaced,  string, { call: [snake.replace, "_", " "] }]
- decl: [pretty,  string, { call: [spaced.title] }]
# pretty == "Smith Touch Master"
```

Single-step `.title()` examples:

```yaml
- decl: [a, string, { call: [{ literal: "hello world", type: string }.title] }]
# a == "Hello World"
- decl: [b, string, { call: [{ literal: "WORKOUT",     type: string }.title] }]
# b == "Workout"
- decl: [c, string, { call: [{ literal: "foo_bar",     type: string }.title] }]
# c == "Foo_bar"   — separator NOT replaced; chain replace first
```

---

### `string.trim()` → `string`

Returns a new string with leading and trailing ASCII whitespace removed.

```yaml
- decl: [raw, string, "   hello world \t"]
- decl: [clean, string]
- set:  [clean, { call: [raw.trim] }]
# clean == "hello world"
```

---

### `string.replace(old, new)` → `string`

Returns a new string where all occurrences of `old` are replaced by `new`.
If `old` is not found, the original content is returned as a new string.

```yaml
- decl: [csv, string, "a,b,b,c"]
- decl: [out, string]
- set:  [out, { call: [csv.replace, "b", "X"] }]
# out == "a,X,X,c"
```

---

### `string.split(delimiter)` → `list<string>`

Splits the string at each occurrence of `delimiter` and returns a `list<string>`.
If the delimiter does not occur, the result contains exactly one element: the original string.

```yaml
- decl: [line, string, "fire earth water"]
- decl: [parts, list<string>]
- set:  [parts, { call: [line.split, " "] }]
# parts.len == 3
# { at: [parts, 0] } == "fire"
# { at: [parts, 1] } == "earth"
# { at: [parts, 2] } == "water"
```

---

### `string.parse_i32()` → `i32`

Parses the string as a base-10 integer using `strtol`. Returns `0` for null, empty, or
non-numeric strings. Handles leading whitespace and optional sign (`+`/`-`).

```yaml
- decl: [s, string, "42"]
- decl: [n, i32, { call: [s.parse_i32] }]
# n == 42

- decl: [neg, string, "-7"]
- decl: [m, i32, { call: [neg.parse_i32] }]
# m == -7

- decl: [bad, string, "hello"]
- decl: [z, i32, { call: [bad.parse_i32] }]
# z == 0
```

> **Difference to `to_int`:** `parse_i32` uses `strtol` (stricter, handles edge cases),
> `to_int` uses `atoi` (simpler, equivalent for well-formed decimal input).
> Both return `0` for null/empty strings.

---

### `string.parse_f32()` → `f32`

Parses the string as a floating-point number using `strtof`. Returns `0.0` for null, empty,
or non-numeric strings. Handles decimal points, scientific notation (`1.5e3`), and signs.

```yaml
- decl: [s, string, "3.14"]
- decl: [f, f32, { call: [s.parse_f32] }]
# f ≈ 3.14

- decl: [sci, string, "1.5e2"]
- decl: [g, f32, { call: [sci.parse_f32] }]
# g == 150.0

- decl: [bad, string, "abc"]
- decl: [h, f32, { call: [bad.parse_f32] }]
# h == 0.0
```

---

### `string.parse_f64()` → `f64`

The same with `strtod`: the whole number, not just the first eight digits.  Returns `0.0` for
null, empty or non-numeric strings and reads the same forms as `parse_f32`.

`parse_f32` + `{ cast: [..., f64] }` is **not** a substitute: the f32 parse has already rounded
the value, and widening cannot bring the digits back.

```yaml
- decl: [txt, string, "0.123456789"]
- decl: [wide, f64, { call: [txt.parse_f64] }]      # 0.123456789
- decl: [narrow, f64, { cast: [{ call: [txt.parse_f32] }, f64] }]
# narrow == 0.123456791043282 — the f32 parse decided this before the cast ran
```

---

## Implicit String → Integer Coercion

When a `string` (or string interpolation expression) is assigned to an `i32` or `i64` variable,
the compiler **automatically inserts `atoi` / `atoll`** to convert the value. No explicit cast
is needed.

This applies to both `decl:` with an initial value and `set:`:

```yaml
- decl: [s, string, "42"]

# decl form
- decl: [n, i32, "${s}"]   # n == 42  (atoi called automatically)

# set form
- decl: [m, i32]
- set:  [m, "${s}"]        # m == 42  (atoi called automatically)

# Also works for i64
- decl: [ts, i64]
- decl: [ts_str, string, "1773000000000"]
- set:  [ts, "${ts_str}"]  # atoll called automatically
```

The coercion is triggered whenever the **runtime type of the value is `ptr<i8>`** (string)
and the **declared type of the target is `i32` or `i64`**.

---

## Escape Sequences

The following escape sequences are supported inside string literals:

| Sequence | Meaning |
|----------|---------|
| `\n` | Newline (LF) |
| `\t` | Horizontal tab |
| `\r` | Carriage return |
| `\\` | Literal backslash |
| `\"` | Double quote |
| `\xHH` | Byte with hex value HH (e.g. `\x41` = `A`) |

All other `\X` sequences are a CompilationError.

**Escapes belong to YAML, not to aiml.**  An aiml program is a YAML document: whatever is in
`"…"` is resolved by the YAML reader, and aiml sees the result.  In a **single**-quoted
scalar (`'…'`) YAML resolves nothing — there `\n` literally stays a
backslash and an `n`.  Measured on 2026-10-18: a shell command built with `'set -e\ntar …'`
arrived as ONE line (`/bin/sh: 1: set: Illegal option -t`).  The rule for
multi-line strings:

- **double** quotes if `\n`/`\t` should take effect — then quotation marks inside
  must be written as `\\\"` for aiml, because YAML already removes the first level;
- **single** quotes if the text should apply literally (shell commands with `"` and `$(…)`)
  — then use **no** backslashes (an unknown `\X` would be a compile error)
  and separate lines with `;` instead of `\n`.
- aiml interpolates `${…}` in **both** forms; `$(…)` and `$var` remain untouched.

## `print`

**Transitional:** `print` is scheduled for removal. New code logs with the `log`
namespace (spec/46); program/tool output that belongs on stdout uses
`io.write(io.stdout, "…")` (spec/23). Existing `print` keeps the rules below until then.

`print: [arg, …]` writes its arguments in order, followed by one newline. A string
argument is written as text (a null string as `(null)`), an integer as decimal.
An argument may be an expression map; a call `{ call: [...] }` must return `string`:

```yaml
- print: [{ call: [<.stats_line] }]          # ok: the method returns string
- print: ["${ { call: [count_items] } }"]    # non-string results go through interpolation
```

A call whose return type is not `string` (number, bool, model, container, handle or
no value) is a CompilationError — interpolate it instead. A `producer`/owning string
result is a statement temporary and is released at statement end (spec/61 §3.2);
a borrowed string result (e.g. a returned member) is not released.
Tests: tests/test_print_call.aiml, tests/test_print_call_nonstring_error.aiml.
