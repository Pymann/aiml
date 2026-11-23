# Globber Builtin Implementation Plan

As of: 2026-04-12

Basis:

- [42_1_globber.md](../spec/42_1_globber.md)
- [42_path_type.md](../spec/42_path_type.md)

Goal:

- a real builtin `Globber`
- wildcards `*`, `?`, `**`
- incremental, directory by directory
- bounded allocation per step
- no full materialisation like `glob.glob(...)`

---

## 1. Starting point in the existing AIML code

### 1.1 What already exists

**Path runtime**

- [path_runtime.h](../src/core/path_runtime.h)
- [path_runtime.cpp](../src/core/path_runtime.cpp)

Already present:

- `aiml_path_intern`
- `aiml_path_join`
- `aiml_path_parent`
- `aiml_path_name`
- `aiml_path_stem`
- `aiml_path_extension`
- `aiml_path_root`
- `aiml_path_relative_to`
- `aiml_io_exists`
- `aiml_io_is_dir`
- `aiml_io_is_file`
- `aiml_io_read_file`
- `aiml_io_write_file`
- `aiml_io_list_dir_fill`

**Compiler/builtins**

- [builtin_registry.cpp](../src/core/builtin_registry.cpp)

Already present:

- `path` as a builtin class
- the `path` namespace (`from_string`, `cwd`, `home`, ...)
- `io.list_dir(path) -> list<path>`
- established builtin classes with opaque runtime handles such as:
  - `process`
  - `socket_handle`

**Tests**

- [test_path.aiml](../tests/test_path.aiml)
- [test_io_write_listdir.aiml](../tests/test_io_write_listdir.aiml)

**AIML-side predecessor**

- `test/tul/train/main.aiml`

There, `expand_glob(...)` already exists as a workaround based on:

- `io.list_dir`
- `string.starts_with`
- `string.ends_with`

This is important, because the desired semantics are already needed in practice in the project.

### 1.2 What is still missing

There is currently **no**:

- runtime type `Globber`
- C API for an incremental glob state
- compiler registration for `Globber`
- `io.globber(...)` constructor
- tests for `*`, `?`, `**`

---

## 2. Architecture decision

The existing code base suggests three possible directions:

1. a pure AIML glob on `io.list_dir`
2. `io.glob(pattern) -> list<path>`
3. a real stateful builtin type

The specification has settled on variant 3. That is also the technically right choice.

### Why not `io.glob(...) -> list<path>`

- large trees allocate too much at once
- no visible traversal position
- no natural backpressure
- `**` would easily become uncontrollably expensive

### Why `Globber`

- fits the existing builtin handle strategy
- fits the `path` runtime
- incremental semantics are clearly testable
- large file trees stay manageable

---

## 3. Target API

### 3.1 Builtin class

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

### 3.2 Namespace constructor

```yaml
- decl: [g, Globber]
- set: [g, { call: [io.globber, "docs/**/*.md", 256] }]
```

`io.globber(...)` is only a convenience constructor and must be semantically
equivalent to `decl + init(...)`.

---

## 4. Core technical decision: runtime handle instead of pure struct state

The existing builtin strategy in [builtin_registry.cpp](../src/core/builtin_registry.cpp) uses opaque runtime pointers for stateful builtins:

- `process`
- `socket_handle`

The same pattern should be used for `Globber`.

### Recommended internal form

**Public AIML view**

- `pattern: path`
- `location: path`
- `findings: list<path>`
- `done: i32`

**Internal runtime state**

```cpp
struct AimlGlobber;
```

with, internally, e.g.:

- `pattern_raw`
- `max_findings_per_step`
- `std::deque<GlobTask>` or `std::vector<GlobTask>`
- current overflow state for large individual folders
- parsed pattern segments
- `done`

Important:

- the traversal queue/stack does **not** belong in AIML fields
- only the visible snapshot state belongs in AIML fields

---

## 5. Runtime design

### 5.1 New C API in `path_runtime.h`

New functions:

```cpp
void* aiml_globber_new(void);
void  aiml_globber_free(void* g);
int   aiml_globber_init(void* g, const char* pattern, int max_findings);
int   aiml_globber_next(void* g, const char** out_location, void* list_ptr);
int   aiml_globber_done(void* g);
const char* aiml_globber_pattern(void* g);
```

Optional:

```cpp
int aiml_glob_match_segment(const char* pattern_seg, const char* name);
```

### 5.2 Internal traversal state

Recommended structure:

```cpp
struct GlobTask {
    std::filesystem::path dir;
    size_t segment_index;
};

struct DirCursor {
    std::filesystem::path dir;
    size_t segment_index;
    std::vector<std::filesystem::directory_entry> entries;
    size_t offset;
};

struct AimlGlobber {
    std::string pattern;
    std::vector<std::string> segments;
    int max_findings_per_step;
    int done;
    std::deque<GlobTask> pending;
    std::optional<DirCursor> active_dir;
};
```

### 5.3 Matching semantics

Per segment:

- `*` -> any number of characters
- `?` -> exactly one character
- `**` -> recursive directory wildcard

Important:

- matching happens **per segment**
- not over the whole path string

### 5.4 `next()` semantics in the runtime

`aiml_globber_next(...)`:

1. empties the `list<path>` prepared by the compiler
2. sets `out_location`
3. processes exactly one directory step
4. writes hits into the result list
5. keeps `findings.len <= max_findings_per_step`
6. returns `1` on progress, `0` at the complete end

If a single folder has more hits than `max_findings_per_step`:

- `location` stays on this folder
- the runtime remembers a cursor
- the next `next()` call continues there

That is important, otherwise the per-step limit would be a mere pretence for large folders.

---

## 6. Compiler and builtin integration

### 6.1 New builtin class in `builtin_registry.cpp`

Analogous to `socket_handle` and `process`:

- `ctx.classes["Globber"]`
- `isBuiltin = true`

Methods:

- `init(pattern, max_findings)`
- `next()`

Field accesses:

- `pattern`
- `location`
- `findings`
- `done`

### 6.2 Difficult point: visible fields + hidden handle

For `Globber`, a bare `i8*` as with `socket_handle` is not enough, because
the specification requires visible members.

Therefore it needs either:

1. a real LLVM struct type for `Globber` with:
   - a hidden handle pointer
   - visible AIML fields

or

2. reuse an existing mechanism for builtin-backed class layouts, if one exists

Recommendation:

- `Globber` as a normal class layout type with a hidden first field:
  - `__globber_handle: i8*` (private/internal)
- the visible fields regularly next to it

Then methods can:

- use the handle
- and afterwards synchronise `location`, `findings`, `done`, `pattern`

### 6.3 `io.globber(...)`

Add to the `io` namespace:

```cpp
io.builtinFuncs["globber"]
```

Semantics:

- allocates a `Globber`
- calls `init(...)`
- returns the object

If constructing class values in the builtin path is too expensive:

- support only `decl + g.init(...)` at first
- add `io.globber(...)` in a second sub-phase

---

## 7. Test plan

### 7.1 New tests

New test file:

- `../tests/test_globber.aiml`

Mandatory cases:

1. `*.txt`
   - flat matching in the current folder
2. `test_?.json`
   - `?` wildcard
3. `docs/*.md`
   - segmented matching with a base directory
4. `docs/**/*.md`
   - recursive traversal
5. `max_findings_per_step = 1`
   - overflow cursor for a large individual folder
6. no hit
   - `done` correct, `findings` empty
7. invalid or unreadable start directory
   - a clean end state instead of a crash

### 7.2 Regressions

Existing tests must keep running:

- [test_path.aiml](../tests/test_path.aiml)
- [test_io_write_listdir.aiml](../tests/test_io_write_listdir.aiml)

### 7.3 TUL smoke test

Later replace the existing AIML workaround in `test/tul/train/main.aiml` or check it in parallel:

- `"docs/*.md"` with the old `expand_glob(...)`
- `"docs/*.md"` with the new `Globber`

That is a good practical test.

---

## 8. Thread safety

From [47_thread_safety.md](../spec/47_thread_safety.md) it follows that:

- `path` interning is already globally protected
- `io.list_dir` is classified as a pure function

For `Globber`:

- a single `Globber` handle should be treated as **category 3 / per-handle**
- i.e. **not implicitly thread-safe**
- parallel use of the same `Globber` object from several threads is not a goal

That simplifies the runtime considerably.

---

## 9. Implementation order

### Phase 1 — runtime foundation

1. `path_runtime.h`
   - C API for `AimlGlobber`
2. `path_runtime.cpp`
   - `AimlGlobber` state
   - segment parser
   - segment matcher for `*` / `?`
   - traversal engine for `**`
   - bounded `next()`

### Phase 2 — compiler/builtin

3. `builtin_registry.cpp`
   - `Globber` builtin class
   - methods `init`, `next`
   - visible fields + hidden handle
4. optionally `io.globber(...)`

### Phase 3 — tests

5. `test_globber.aiml`
6. possibly `test_globber_recursive.aiml` if the first test becomes too big
7. TUL smoke test or a temporary example test

### Phase 4 — docs

8. `aiml_reference_guide.md`
   - add `Globber`
9. possibly `00_reference_guide.md`
   - sub-chapter already listed, add a short reference only if needed

---

## 10. Non-goals for V1

Not part of the first implementation:

- character classes `[abc]`
- brace expansion `{a,b}`
- shell expansion of `~`
- a symlink policy beyond the default filesystem iteration
- a sorting guarantee across platforms
- parallel traversal of a single `Globber`

---

## 11. Definition of done

The feature is done when:

1. `Globber.init(pattern, max_findings_per_step)` works
2. `Globber.next()` iterates directory by directory
3. `*`, `?`, `**` are supported correctly
4. `findings.len` is bounded per step
5. large individual folders are processed without full materialisation
6. `location`, `done`, `pattern` are synchronised correctly
7. the new tests are green
8. the existing `path` and `io.list_dir` tests stay green

---

## 12. Recommended first implementation decision

The riskiest technical spot is **not** the wildcard matching, but
the question of how `Globber` is represented in the compiler layout.

Therefore the first concrete implementation spike should be:

1. cleanly model hidden handle + visible fields as a builtin class
2. then connect the runtime `next()`

Once this part sits cleanly, the rest is comparatively straightforward.
