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

All string methods require `call:` (except `len`, which is also a property).

### `string.len` → `i32`

Returns the number of **characters** (Unicode code points). As a property, no `call:` is needed.

```yaml
- decl: [s, string, "Hello"]
- decl: [n, i32]
- set:  [n, s.len]              # property form — no call: needed
- set:  [n, { call: [s.len] }]  # call form — also valid
```

> Note: `sys.size(s)` returns the **byte count** (`u64`) instead.

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
