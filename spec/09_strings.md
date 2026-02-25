# String Expansion / Interpolation

```yaml
# Runtime Expansion is the default.
# Const variables may be optimized to compile-time constants.

# Syntax: "${var}"
- decl: [name, string]
- set: [name, "Alice"]
- decl: [age, i32]
- set: [age, 30]

- print: ["Hello ${name}, you are ${age} years old."]

# Complex Expressions inside ${ }
# See `09_1_string_expansions.md` for full details.
- print: ["Next year: ${ { +: [age, 1] } }"]

# String Length vs. Size
#
# 1. Byte Count (sys.size) — intrinsic method, requires call:
#    Returns the number of bytes in memory (UTF-8 encoded). Returns u64.
- decl: [byte_count, u64]
- set: [byte_count, { call: [sys.size, name] }]

# 2. Character Count (string.len) — property, no call: required
#    Returns the number of Unicode code points. Returns i32.
#    "A" -> 1 char, 1 byte
#    "€" -> 1 char, 3 bytes
- decl: [char_count, i32]
- set: [char_count, name.len]
```

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
