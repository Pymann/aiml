# Builtin Globber

This chapter defines AIML's filesystem globbing: a stateful, incremental walker
(`Globber`) for large trees, `io.glob` for the common small case, and
`io.glob_match` for testing one path against a pattern.

The walker:

- supports real wildcard matching (`*`, `?`, `[…]`, `**`)
- never holds more than one directory's worth of results
- exposes the current traversal location
- returns matches directory-by-directory

---

## 1. Motivation

Simple glob APIs often have two problems:

1. They allocate the full result set eagerly.
2. They hide traversal state, which makes large scans opaque and expensive.

For a scan of a whole tree (a build tool looking for sources below a big
checkout, an indexer) the walker is the right tool.  For a handful of files
(`*.json` in a config directory) `io.glob` is simpler and just as good.

---

## 2. API (F42-1-1)

| Call | Returns | Meaning |
|------|---------|---------|
| `io.globber(pattern, max_per_step)` | `Globber` | a walker for `pattern`; `max_per_step` < 1 counts as 1 |
| `g.next()` | `list<string>` | the next batch of matches — all from one directory, at most `max_per_step`; an **empty list means the walk is over** |
| `g.location` | `string` | the directory the last batch came from (`""` before the first `next`) |
| `g.done` | `i32` | `1` once `next` returned the empty list |
| `g.pattern` | `string` | the pattern as given |
| `g.free()` | — | releases the walker (call once) |
| `io.glob(pattern)` | `list<string>` | every match, sorted by byte order (F42-1-2) |
| `io.glob_match(pattern, text)` | `i32` | `1` if the path `text` matches `pattern` — no filesystem access (F42-1-3) |

```aiml
- decl: [walker, Globber, { call: [io.globber, "src/**/*.cpp", 256] }]
- decl: [batch, list<string>, { call: [walker.next] }]
- while:
    condition: { gt: [batch.len, 0] }
    body:
      - print: ["in ${walker.location}: ${batch.len} files"]
      - set: [batch, { call: [walker.next] }]
- call: [walker.free]

- decl: [configs, list<string>, { call: [io.glob, "conf/*.json"] }]
- if: { condition: { eq: [{ call: [io.glob_match, "**/test_*.aiml", name] }, 1] }, then: [...] }
```

A directory with more matches than `max_per_step` is returned over several
`next` calls; `location` stays the same until its matches are drained.  A
directory without matches yields no batch (it is skipped, not an empty batch).

---

## 3. Wildcard Semantics (F42-1-4)

Matching is **per path segment** (`/` separates segments; a wildcard never
matches `/`):

| Form | Matches |
|------|---------|
| `*` | zero or more characters within one segment |
| `?` | exactly one character within one segment |
| `[abc]` `[a-z]` `[!abc]` | one character of / not of the set |
| `**` | as a whole segment: zero or more directory levels |
| any other character | itself; `\x` matches `x` literally |

- A wildcard does not match a **leading dot**: `*` does not match `.git`,
  `*.cpp` does not match `.hidden.cpp`, and `**` does not descend into hidden
  directories.  A segment that itself starts with `.` (`.*`, `.github/**`) does.
- A **trailing `**`** (`build/**`) matches every file and directory below its
  base, recursively.
- Symbolic links are reported like other entries, but `**` does not follow a
  link to a directory (no cycles).
- `.` and `..` never match a wildcard.

```text
"*.aiml"        all .aiml files in the current directory
"docs/*.md"     markdown files directly in docs/
"test_?.json"   test_a.json, test_1.json, …
"src/**/*.cpp"  .cpp files anywhere below src/ (src/a.cpp included)
"lib[0-9].so"   lib1.so … lib9.so
```

Not supported: brace expansion `{a,b}`, `~` expansion.

---

## 4. Result Paths (F42-1-5)

Results are the pattern's literal prefix joined with what matched:

- `src/**/*.cpp` → `src/main.cpp`, `src/core/x.cpp`
- `*.json` → `a.json` (no `./` prefix)
- `/etc/*.conf` → `/etc/host.conf` (absolute stays absolute)

Within one batch, entries are sorted by byte order; directories are walked
depth-first in byte order, so the overall order is deterministic.

---

## 5. Failure Semantics (F42-1-6)

- a base directory that does not exist or cannot be read: the first `next`
  returns the empty list, `io.glob` an empty list
- an unreadable directory deeper in the walk is skipped; the walk continues
- an empty pattern matches nothing; a malformed class (`[a-`) matches its
  characters literally

---

## 6. Design Rationale

Why is `next` the carrier of the results rather than a `findings` member?

- a batch is a value the caller owns — it can be kept, passed on, or dropped
  without aliasing the walker's internal state
- "empty batch = finished" makes the loop a single condition

Why expose `location`?

- it makes scanning visible (progress reporting, debugging)
- callers can group results by directory without re-splitting paths

Why both `io.globber` and `io.glob`?

- the walker bounds memory for big trees; `io.glob` keeps the small case a
  one-liner.  Both use the same matcher, so a pattern means the same in each.
