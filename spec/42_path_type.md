# Builtin Path Type

See also: [42_1_globber.md](42_1_globber.md)

## Motivation: The String-Path Problem

In most languages a file path is a `string`. This leads to a series of structural
problems:

**1. No type safety.** A function that expects a path receives a string — but which
kind? An absolute one? A relative one? A URL? A filename without a directory? The
compiler does not know, and the programmer forgets.

**2. Unnecessary copying.** Every string operation creates a new heap allocation.
`path + "/" + filename` allocates three times: first `path + "/"`, then the result +
`filename`. For something as common as path concatenation this is wasteful.

**3. Redundant representations.** The path lives as a `std::string` in a map. Then
it is passed as `char*` to `open()`. Then as `std::filesystem::path` for operations.
Then back as `std::string` for logging. Every conversion is a copy.

**4. No normalization.** `"/usr/local/../local/bin"` and `"/usr/local/bin"` are
different as strings but identical as paths. Without explicit normalization the same
path ends up in different representations throughout the system.

**5. No compile-time resolution.** Static paths (config files, templates, known system
paths) are known at compile time — but are still re-parsed and re-allocated at runtime
every time.

AIML solves this with a **first-class path type**: `path` is a primitive like `i32`
or `bool` — not a wrapper struct, not a string alias, but its own type with its own
guarantees.

---

## Core Principles

### 1. Interning — One path, one memory address

AIML interns all paths in a global **path intern table**: two paths with the same
normalized content share *exactly the same memory*. A `path` value is internally
just a 64-bit pointer into this table — no heap buffer, no length, no capacity field.

```
path p1 = "/usr/local/bin"   →  intern_table[42]  →  "/usr/local/bin\0"
path p2 = "/usr/local/bin"   →  intern_table[42]  (same pointer!)
path p3 = "/usr/local/../local/bin"  →  normalized → intern_table[42]  (same!)
```

**Consequence:** Path comparison is a **pointer comparison** (O(1), no character
loop). Path copy is an **integer copy** (8 bytes, no malloc). A path map
`map<path:T>` uses pointers as keys — no string hashing.

### 2. Immutability

A `path` value is immutable. `path.join(...)` produces a *new* interned path — without
modifying the old one and without unnecessary intermediate buffers (the concatenation
happens once, the result is immediately interned).

### 3. No implicit string conversion

`path` cannot be used silently as `string`. A function that expects `string` does not
accept a `path` — and vice versa. Explicit conversion is always visible: `p.to_string()`.

### 4. Compile-time interning for static paths

Path literals that are known at compile time are entered into the intern table at program
startup — no runtime allocation, no parsing at runtime.

```yaml
# These two paths are interned ONCE at program startup,
# after which set is just an integer store:
- decl: [cfg, path]
- set: [cfg, "/etc/myapp/config.toml"]   # compile-time interned literal
```

---

## Type Declaration & Literals

```yaml
# path is a primitive — declared like i32/bool
- decl: [p, path]

# Path literal: normal string syntax, but target type is path → no string allocated
- set: [p, "/usr/local/bin"]

# Relative path literal
- decl: [rel, path]
- set: [rel, "config/app.toml"]

# Empty path (null equivalent)
- decl: [empty, path]    # default: path.empty (= intern_table[0])
```

### Path Type Variants

```yaml
# path       — general path (absolute or relative)
# abspath    — subtype: guaranteed absolute (compiler error if relative)
# relpath    — subtype: guaranteed relative
# dirpath    — subtype: does not end with a file component (directory only)
# filepath   — subtype: has a file component (leaf node)

- decl: [abs, abspath]
- set: [abs, "/usr/local/bin"]     # OK
- set: [abs, "relative/path"]      # COMPILE ERROR: abspath requires absolute path

- decl: [dir, dirpath]
- set: [dir, "/usr/local/bin/"]    # OK (trailing slash or explicitly as dir)
- set: [dir, { call: [p.parent] }] # OK: parent() always returns dirpath
```

---

## Operations

### Composition

```yaml
# path.join — joins paths, normalizes the result, interns it once
- decl: [base, path]
- set: [base, "/usr/local"]

- decl: [full, path]
- set: [full, { call: [base.join, "bin", "aiml"] }]
# Result: "/usr/local/bin/aiml" — NO intermediate buffer for "/usr/local/bin"

# Operator / as syntactic sugar for join (compile-time desugaring)
- set: [full, { path: [base, "bin", "aiml"] }]   # equivalent to join

# join with path variables (not just strings)
- decl: [suffix, path]
- set: [suffix, "bin/aiml"]
- set: [full, { call: [base.join, suffix] }]
# If suffix were absolute, base would be discarded (POSIX semantics)
```

### Decomposition

```yaml
- decl: [p, path]
- set: [p, "/usr/local/bin/aiml.elf"]

# Directory component (parent)
- decl: [dir, path]
- set: [dir, { call: [p.parent] }]          # "/usr/local/bin"

# Filename (last component)
- decl: [name, string]
- set: [name, { call: [p.name] }]           # "aiml.elf"

# Filename without extension
- decl: [stem, string]
- set: [stem, { call: [p.stem] }]           # "aiml"

# Extension (with dot)
- decl: [ext, string]
- set: [ext, { call: [p.extension] }]       # ".elf"

# All components as a list
- decl: [parts, list<string>]
- set: [parts, { call: [p.parts] }]         # ["usr", "local", "bin", "aiml.elf"]

# Number of components
- decl: [depth, i32]
- set: [depth, { call: [p.depth] }]         # 4

# Root component
- decl: [root, path]
- set: [root, { call: [p.root] }]           # "/"  (or "C:\" on Windows)
```

### Properties

```yaml
- decl: [p, path]
- set: [p, "/usr/local/bin/aiml"]

- decl: [is_abs, bool]
- set: [is_abs, { call: [p.is_absolute] }]  # true

- decl: [is_rel, bool]
- set: [is_rel, { call: [p.is_relative] }]  # false

- decl: [is_empty, bool]
- set: [is_empty, { call: [p.is_empty] }]   # false

# Normalized path (.. and . resolved, double slashes removed)
# Happens automatically during interning — but can be called explicitly:
- decl: [norm, path]
- set: [norm, { call: [p.normalize] }]      # identical to p (already normalized)

- decl: [messy, path]
- set: [messy, "/usr//local/../local/./bin"]
- set: [norm, { call: [messy.normalize] }]  # "/usr/local/bin"
# (normalize already happens automatically on set — norm == intern("/usr/local/bin"))

# Compute relative path from base to p
- decl: [base, path]
- set: [base, "/usr/local"]
- decl: [rel, path]
- set: [rel, { call: [p.relative_to, base] }]   # "bin/aiml"
```

### Comparison

```yaml
# Comparison is O(1) pointer comparison
- decl: [a, path]
- decl: [b, path]
- set: [a, "/usr/local/bin"]
- set: [b, "/usr/local/../local/bin"]   # normalized → same pointer as a

- decl: [eq, bool]
- set: [eq, { binary: [a, ==, b] }]    # true — no string comparison, just ==ptr

# Lexicographic ordering (for sorting in lists)
- decl: [lt, bool]
- set: [lt, { binary: [a, <, b] }]
```

### Conversion

```yaml
# Explicit conversion to string (the only permitted string conversion)
- decl: [s, string]
- set: [s, { call: [p.to_string] }]         # allocates only when necessary (lazy)

# string → path (parses and normalizes)
- decl: [from_str, path]
- set: [from_str, { call: [path.from_string, s] }]

# To OS-native representation (backslashes on Windows)
- decl: [os_str, string]
- set: [os_str, { call: [p.to_os_string] }]
```

---

## Filesystem Integration

The `io` namespace accepts only `path`, never `string`:

```yaml
# CORRECT:
- decl: [p, path]
- set: [p, "/etc/hosts"]
- decl: [content, string]
- set: [content, { call: [io.read_file, p] }]

# COMPILE ERROR — string is not path:
- set: [content, { call: [io.read_file, "/etc/hosts"] }]
#                                        ^^^^^^^^^^^^ Type mismatch: string != path
# Exception: path context inference — literals in a path context are parsed as path:
# (if io.read_file expects path, "/etc/hosts" is a path literal, not a string literal)

# io.read_file(p) → string: the whole file content as an owned string (spec/61 §7);
# "" when the file cannot be read (use io.is_file to tell a missing file from an empty one).
#
# Reads up to EOF, not up to the reported SIZE (corrected 2026-10-17).  Virtual
# files under /proc and /sys report size 0 and still deliver content; the earlier
# version sized the buffer via `ftell` and therefore ALWAYS returned "" there.  Found
# when cosmos (doc/spec/109) wanted to read its own CPU load from `/proc/self/stat` and
# got an empty string — without an error, which made the search expensive.
# For ordinary files it stays at one allocation and one `fread`.

# Filesystem operations
- decl: [exists, bool]
- set: [exists, { call: [io.exists, p] }]

- decl: [is_dir, bool]
- set: [is_dir, { call: [io.is_dir, p] }]

- decl: [is_file, bool]
- set: [is_file, { call: [io.is_file, p] }]

- decl: [size, i64]
- set: [size, { call: [io.file_size, p] }]

# Directory contents (returns list<path> — no strings!)
- decl: [entries, list<path>]
- set: [entries, { call: [io.list_dir, p] }]
- for_each:
    var: entry
    in: entries
    body:
      - print: ["${entry}"]     # path has an implicit display representation in interpolation

# Create directory
- call: [io.mkdir, p]
- call: [io.mkdir_all, p]    # mkdir -p

# Rename / move (atomic on POSIX)
- decl: [dest, path]
- set: [dest, "/etc/hosts.bak"]
- call: [io.rename, p, dest]
# Hard link: a second name for the same content, without a copy (0 = ok, -1 = error,
# e.g. across file system boundaries or if the target exists).
- decl: [linked, i32, { call: [io.link, p, dest] }]

# Delete
- call: [io.remove, p]
- call: [io.remove_all, p]   # rm -rf (directory tree)
```

### File Descriptor Caching (`file_ref`)

A `file_ref` combines a `path` with an open file descriptor. This way the path does not
need to be resolved for every I/O call (no repeated `open()`):

```yaml
# file_ref — path + open fd, no repeated lookup on read/write
- decl: [f, file_ref]
- set: [f, { call: [io.open, p, "r"] }]   # opens once, keeps fd open

- decl: [line, string]
- set: [line, { call: [f.read_line] }]

- call: [f.seek, 0]
- call: [f.write, "new content"]

# Retrieve the path from a file_ref (zero-cost, stored internally)
- decl: [orig_path, path]
- set: [orig_path, { call: [f.path] }]

- call: [f.close]
```

---

## Path Security

### Traversal Protection

```yaml
# path.safe_join — rejects components that lead outside the base directory
# Protects against path traversal attacks (../../etc/passwd)

- decl: [base, path]
- set: [base, "/var/www/uploads"]

- decl: [user_input, string]
- set: [user_input, "../../etc/passwd"]    # attacker input

- decl: [safe, path]
- set: [safe, { call: [base.safe_join, user_input] }]
# RUNTIME ERROR: "../../etc/passwd" escapes base "/var/www/uploads"

# Explicitly allowing relative ../ components (opt-in):
- set: [safe, { call: [base.join_unchecked, user_input] }]  # opt-in: no protection
```

### Permission Checks

```yaml
# Check whether the current process has access (uses access(2))
- decl: [can_read, bool]
- set: [can_read, { call: [io.can_read, p] }]

- decl: [can_write, bool]
- set: [can_write, { call: [io.can_write, p] }]

- decl: [can_exec, bool]
- set: [can_exec, { call: [io.can_exec, p] }]
```

---

## Paths in Data Structures

Since `path` is internally an 8-byte pointer, paths in collections are extremely
efficient:

```yaml
# map<path:T> — lookup is O(1) pointer comparison, no string hashing
- decl: [cache, map<path:string>]
- call: [cache.set, p, "cached content"]
- decl: [hit, string]
- set: [hit, { call: [cache.get, p] }]   # O(1), no string comparison

# list<path> — 8 bytes per entry (no char*, no length, no capacity)
- decl: [paths, list<path>]
- call: [paths.push, "/usr/local/bin/aiml"]
- call: [paths.push, "/usr/local/lib/libaiml.so"]

# set<path> — deduplication by pointer (intern guarantees uniqueness)
- decl: [visited, set<path>]
- call: [visited.add, p]
- decl: [seen, bool]
- set: [seen, { call: [visited.contains, p] }]   # O(1)
```

---

## Platform Abstraction

`path` is internally platform-agnostic: `/` as separator on all platforms,
`to_os_string()` converts for platform APIs:

```yaml
# Platform-specific well-known paths
- decl: [home, path]
- set: [home, { call: [path.home] }]         # ~/  (expanded from $HOME / USERPROFILE)

- decl: [tmp, path]
- set: [tmp, { call: [path.temp_dir] }]      # /tmp  or  C:\Users\X\AppData\Local\Temp

- decl: [cfg, path]
- set: [cfg, { call: [path.config_dir, "myapp"] }]
# Linux: ~/.config/myapp  |  macOS: ~/Library/Application Support/myapp  |  Win: AppData\Roaming\myapp

- decl: [exe, path]
- set: [exe, { call: [path.current_exe] }]   # path of the running program (readlink /proc/self/exe)

- decl: [cwd, path]
- set: [cwd, { call: [path.cwd] }]           # current working directory
```

---

## Internal Structure (Implementation)

```
path value (runtime):
  ┌─────────────────────────────┐
  │  u64: pointer → intern_entry │   8 bytes — that is all
  └─────────────────────────────┘

intern_entry (intern table):
  ┌──────────┬─────────┬─────────────────────────────┐
  │  u32 len │ u32 hash│  char[] normalized_utf8\0   │
  └──────────┴─────────┴─────────────────────────────┘

Intern table:
  - Hash map: FNV-1a(normalized_str) → intern_entry*
  - Lock-free insert (CAS on bucket pointer)
  - Entries never deleted (paths are program-lifetime data)
  - Compile-time paths embedded in read-only section (no runtime malloc)
```

**Comparison with string:**

| Operation          | `string`          | `path`                  |
|--------------------|-------------------|-------------------------|
| Copy               | malloc + memcpy   | 8-byte integer copy     |
| Comparison         | O(n) memcmp       | O(1) pointer comparison |
| Hash               | O(n) computed     | Already stored in entry |
| Join               | 2+ allocations    | 1 allocation (result)   |
| As map key         | string hash + cmp | pointer hash + ==       |
| Normalization      | Manual / never    | Automatic on intern     |
| Type safety        | None              | Compile-time            |
