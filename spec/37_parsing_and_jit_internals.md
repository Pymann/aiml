# AIML Compiler Challenges: Parsing and JIT Internals

This document explains two critical categories of challenges faced during the AIML compiler
development and how they were resolved.

## 1. Syntax Conflicts with YAML

Because AIML uses YAML as its structural syntax, we face unique parsing challenges when
embedding complex expressions or types that clash with YAML's own special characters.

### The Map Type Problem (`map<K:V>`)
YAML uses commas `,` as delimiters in flow-style collections (e.g., `[a, b]`).
- **Conflict**: A declaration like `map<string, i32>` contains a comma.
- **Issue**: YAML parsers interpret `map<string` as one value and ` i32>` as another.
- **Solution**: We enforce `map<Key:Value>` using a colon as the type separator.

**Parsing Rule**: The `:` inside `<>` angle brackets is the **map type separator**, not
a YAML key-value separator. It is only interpreted this way when encountered inside a
`<>` type parameter context. Outside `<>`, `:` retains standard YAML semantics.

This is handled in the type parser before the YAML node is fully evaluated.

### String Expansion and Flow Maps
AIML allows dynamic string interpolation using `${...}`.
- **Scenario**: `"Result: ${ { call: [add, 10, 20] } }"`
- **Conflict**: The inner `{ ... }` is standard YAML for Flow Mappings.
- **Solution**: We extract the `${...}` content, trim whitespace, then pass it to a
  secondary YAML parse step via `StringUtils::expandExpression`.

This is valid and has been confirmed to work correctly. No alternative delimiter is needed.

---

## 2. JIT Compilation and Execution

### Target Registration (`InitializeAllTargets`)
- **Issue**: LLVM does not initialize any backends by default.
- **Fix**: Call `llvm::InitializeAllTargets()` and related functions. Set the module's
  Target Triple to the host machine (e.g., `x86_64-pc-linux-gnu`).

### Dynamic Symbol Loading (`LoadLibraryPermanently`)
- **Issue**: In JIT mode, generated code runs in the compiler's own process. Standard C
  functions (`printf`, `malloc`, `exit`) are not automatically resolved.
- **Fix**: Call `llvm::sys::DynamicLibrary::LoadLibraryPermanently(nullptr)` to expose
  the compiler's linked C standard library to JIT-executed code.

---

## 3. Import Cache (`__aiml_cache`)

Used in JIT/`-r` mode only. AOT/exodus builds bypass it (`noCache=true`) and own the
build graph themselves.

### Cache Key

An import's `.o` is reusable only if **everything the compiler reads** to produce it
is unchanged. The key is `sha256` (hex) over a normalized text block of:

1. `source_path`        (canonicalized)
2. `source_hash`        (sha256 of file content)
3. `transitive_hash`    (sorted `path|sha256` of all transitive imports)
4. `sibling_hash`       (sorted `path|sha256` of all `siblingImports`)
5. `library_mode`       (header-only / library vs entrypoint)
6. `compiler_rev`       (size + mtime of the `aiml` binary itself)

A path-only key produces false hits whenever a sibling or transitive import changes —
sub-compilers see those types injected into their context, so the produced IR depends
on them.

All hashing is done in-process (LLVM `SHA256`); computing a key spawns no processes
and writes nothing to stdout.

### Layout

```
<cache>/                           # $AIML_CACHE, default ./__aiml_cache
  rev_<compiler_rev>/              # one directory per compiler binary revision
    <sanitized_path>__<key32>.o    # key32 = first 32 hex chars of the cache key
```

`<sanitized_path>` is the import path with every non-alphanumeric character replaced
by `_`. Different semantic contexts of the same source get different keys and
therefore different object files; any number of them coexist.

### Hit Rule

An import is a cache **hit** iff its keyed object file exists. The file name carries
the key, so there is no manifest and no other lookup state: runs with different
sibling sets never evict each other's objects.

### Publishing

An object is compiled to a temporary file in the same directory
(`<object>.tmp.<pid>`) and published by an atomic `rename` **only if the import
compiled without errors**. Consequences:

- A failed compile publishes nothing. The next run misses, recompiles and reports the
  same error again — it never loads a partial or empty object.
- Concurrent `aiml` invocations sharing one cache are safe. Two writers of the same
  key produce equivalent objects; whichever `rename` lands last wins, readers see
  either no file or a complete one.
- A killed run leaves at most a stale `*.tmp.*` file, which is never read and is
  removed by the revision sweep below.

### Revision Sweep

At startup the compiler touches its own `rev_<compiler_rev>/` directory and removes
every other `rev_*` directory not touched for more than one hour, plus `*.tmp.*` files
older than one hour in its own directory. Rebuilding the
compiler therefore frees the previous revision's objects automatically, while a
second compiler binary used concurrently on the same cache (e.g. a worktree build)
keeps its directory.

### Per-Run Compilation Guard

Each `AIMLCompiler` instance owns a `compiledKeys` set (shared with its sub-compilers
via `shared_ptr`). It is keyed on the full `cache_key`, not the path, so diamond
imports under different sibling sets do not cross-pollinate. There is no
process-wide static guard.

### Cycle Protection

`collectTransitiveImports` uses a `visited` set keyed on canonicalized paths to
guard against `A imports B, B imports A`.
