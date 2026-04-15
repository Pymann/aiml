# Builtin Globber

This chapter defines AIML's stateful filesystem globbing API.

The goal is not a one-shot `glob() -> list<path>` materialization, but an
incremental directory walker that:

- supports real wildcard matching
- avoids loading the whole result set at once
- exposes the current traversal location
- returns matches directory-by-directory

This is intentionally closer to a bounded iterator than to Python's eager
`glob.glob(...)`.

---

## 1. Motivation

Simple glob APIs often have two problems:

1. They allocate the full result set eagerly.
2. They hide traversal state, which makes large scans opaque and expensive.

For AIML, that is the wrong default. The language already has a first-class
`path` type and directory listing support. A stateful builtin integrates better
with the rest of the filesystem model.

---

## 2. Core Type

```yaml
- class:
    name: Globber
    members:
      - [pattern, path, 0, { access: public }]
      - [location, path, 0, { access: public }]
      - [findings, list<path>, 0, { access: public }]
      - [done, i32, 0, { access: public }]
    methods:
      - def:
          name: init
          access: public
          args: [[pattern, path], [max_findings_per_step, i32]]
          return: void
      - def:
          name: next
          access: public
          args: []
          return: i32
```

### Member Semantics

- `pattern`
  - normalized input glob pattern
- `location`
  - current directory being processed by the last successful `next()`
- `findings`
  - matches found in the current step
  - always limited to the current directory step
- `done`
  - `0` while traversal is still active
  - `1` once the traversal is exhausted

---

## 3. Initialization

```yaml
- decl: [g, Globber]
- call: [g.init, "docs/**/*.md", 256]
```

### `init(pattern: path, max_findings_per_step: i32)`

Initializes a fresh traversal state.

- `pattern`
  - may contain wildcard segments
  - is stored in normalized `path` form
- `max_findings_per_step`
  - upper bound for `findings.len` after each `next()`
  - must be `> 0`
  - implementation may clamp invalid values to a safe minimum such as `1`

Initialization clears:

- previous `findings`
- previous traversal queues/stacks
- previous `location`
- previous `done` state

---

## 4. Incremental Traversal

### `next() -> i32`

Advances the traversal by one directory step.

Return value:

- `1`
  - a traversal step was executed
  - `location` and `findings` are updated
- `0`
  - traversal is finished
  - `done == 1`
  - `findings` is empty

### Step Semantics

Each successful `next()`:

1. clears `findings`
2. selects the next directory to process
3. sets `location` to that directory
4. scans entries in that directory
5. appends matching result paths to `findings`
6. schedules further directories if required by the pattern

This means:

- results are produced **directory by directory**
- not all matches are returned at once
- traversal state remains explicit and inspectable

---

## 5. Wildcard Semantics

Wildcard matching is defined **per path segment**, not on the fully joined path string.

Supported patterns:

- `*`
  - matches zero or more characters within one segment
- `?`
  - matches exactly one character within one segment
- `**`
  - recursive directory wildcard
  - matches zero or more directory levels

### Examples

```yaml
"*.aiml"        # all .aiml files in current directory
"docs/*.md"     # all markdown files directly in docs/
"test_?.json"   # test_a.json, test_1.json, ...
"src/**/*.cpp"  # all .cpp files recursively below src/
```

Not part of this spec revision:

- character classes like `[abc]`
- brace expansion like `{a,b}`
- shell-style tilde expansion in glob patterns

These may be added later.

---

## 6. Result Rules

### Directory Granularity

`findings` must only contain results associated with the current `location`
step. A single `next()` must not aggregate results from multiple unrelated
directories.

### Per-Step Limit

If more matches exist in the current directory than `max_findings_per_step`
allows, the implementation may either:

1. continue that directory over multiple internal sub-steps, or
2. keep an internal overflow cursor for the same `location`

but it must preserve these observable invariants:

- `location` remains stable while unfinished results of that directory remain
- `findings.len <= max_findings_per_step`
- no match is silently dropped

---

## 7. Suggested Runtime Model

The runtime will typically maintain an internal queue or stack of states like:

```text
(directory_path, segment_index)
```

This is not exposed directly to AIML code, but it is the natural execution
model for:

- recursive `**`
- multi-segment traversal
- bounded incremental output

The public API stays intentionally small:

- `init(...)`
- `next()`
- `pattern`
- `location`
- `findings`
- `done`

---

## 8. Namespace Construction

The preferred constructor is exposed from `io`:

```yaml
- decl: [g, Globber]
- set: [g, { call: [io.globber, "docs/**/*.md", 256] }]
```

Equivalent explicit style:

```yaml
- decl: [g, Globber]
- call: [g.init, "docs/**/*.md", 256]
```

If both forms are implemented, they must be semantically equivalent.

---

## 9. Example Usage

```yaml
- decl: [g, Globber]
- call: [g.init, "docs/**/*.md", 128]

- while:
    condition: { eq: [{ call: [g.next] }, 1] }
    body:
      - print: ["Scanning: ${g.location.to_string}"]
      - decl: [i, i32, 0]
      - while:
          condition: { "<": [i, g.findings.len] }
          body:
            - decl: [p, path, { call: [g.findings.get, i] }]
            - print: ["  match: ${p.to_string}"]
            - set: [i, { +: [i, 1] }]
```

---

## 10. Failure Semantics

- invalid or unreadable base directory
  - `done = 1`
  - `findings = []`
  - first `next()` returns `0`
- malformed pattern
  - should be rejected during `init(...)` if detectably invalid
  - otherwise treated as a non-matching traversal
- permission errors during traversal
  - affected directory may be skipped
  - traversal continues when safe to do so

The runtime should prefer partial forward progress over global failure.

---

## 11. Design Rationale

Why not `io.glob(pattern) -> list<path>`?

Because:

- large trees can allocate too much at once
- traversal becomes opaque
- incremental workflows become harder

Why expose `location`?

Because:

- it makes scanning visible
- it supports progress reporting
- it helps incremental processing and debugging

Why keep `findings` public?

Because:

- AIML code can process each batch directly
- no second iterator protocol is required
- it stays simple and explicit

---

## 12. Minimum Conformance

An implementation is conformant if it provides:

- `Globber.init(pattern, max_findings_per_step)`
- `Globber.next()`
- `pattern`, `location`, `findings`, `done`
- wildcard support for `*`, `?`, and `**`
- directory-by-directory incremental output
- bounded `findings` allocation per step

