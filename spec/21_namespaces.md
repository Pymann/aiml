# Modules and Namespaces

## 1. Defining a Namespace
The `namespace` command sets the namespace for the **entire file**.
It is typically the first command in a file.

**Reserved namespaces** — the following prefixes cannot be used as user-defined namespaces;
any such namespace declaration raises a CompilationError:
- `sys`
- `time`
- `io`

```yaml
# Sets namespace for all subsequent definitions in this file
- namespace: my.math

- def:
    name: calc  # Becomes "my.math.calc"
    ...
```

## 2. Symbol Visibility

- **Namespace-level symbols** (top-level functions, classes, models, enums) are **public** by default.
- **Class members** are **private** by default (see `03_modifiers.md`).
- Duplicate FQNs across imported files raise `NamespaceCollisionError` at compile time.

### 2.1 Visibility Scope — the import closure

A file sees its own declarations plus the public declarations of every file in its
**transitive import closure** (the files it imports, the files those import, …) —
and nothing else.  Being part of the same program is not enough: if `main.aiml`
imports both `a.aiml` and `b.aiml`, then `a.aiml` sees `b.aiml` only if it imports it
(directly or transitively).  Using a symbol from outside the closure is a compile
error in the using file.

Consequently a file compiles to the same object in every program that includes it;
its compiled form depends only on its own closure (see the cache key,
`37_parsing_and_jit_internals.md` §3).

Class groups follow the same rule; for `groupmethods:` files see
`58_classgroups.md` §1.1.

## 3. Importing Modules
The `import` command loads another AIML file.
- Each file is compiled **at most once** — the `__aiml_cache` tracks already-compiled files.
- Circular imports are therefore **allowed**. The cache prevents infinite loops.
- Import paths are **relative to the importing file's directory**. `../` is supported.
- **Absolute paths in import statements are forbidden.**
- Imported symbols must be accessed via their **Fully Qualified Name (FQN)**.

```yaml
- import: "utils/math.aiml"

- def:
    name: main
    body:
      - call: [my.math.calc]
```

## 4. Execution vs Compilation

### 4.1 Direct Execution (`aiml main.aiml -r` or `aiml main.aiml --run`)
- **Behavior**: JIT compilation with Object Caching.
- **Imports**:
    1. Imported files are checked against valid cache in `__aiml_cache`.
    2. If missing or outdated, compiled to native object files (`.o`).
    3. The main file is compiled to LLVM IR in memory.
    4. Imported object files are loaded into the JIT execution engine.
- **Result**: Hybrid execution — dependencies pre-compiled, main script JIT-compiled.

### 4.2 Object Compilation (`aiml main.aiml -o output.o`)
- **Behavior**: Separate Compilation (used by `exodus build`).
- **Imports**:
    1. Checks if imported file needs recompilation (missing or older `.o`).
    2. Compiles imported file to a native object file if needed.
    3. Main file compiled to `.o`.
- **Linking**: Handled by the build system (exodus). The compiler writes a `.deps` sidecar
  file listing all transitive import object files. exodus reads this and links all objects
  with `g++`/`clang` plus libraries from `exodus.json` (e.g. `libaiml.so`, `libssl`, etc.).
- **Note**: Standalone `aiml file.aiml` (without `-r` or `-o`) only generates `.ll` (LLVM IR).
  Full AOT binaries require `exodus build` with an `exodus.json` config.

## 5. Paths and Caching

- **Import Paths**: Relative to the directory of the importing file.
    - `import: "src/utils/math.aiml"` → resolved from the importing file's directory.
- **JIT Cache Directory** (`-r` mode only): `__aiml_cache/` in the CWD (or `$AIML_CACHE` if set).
  Objects are content-keyed; key, layout, hit rule and publishing are specified in
  `37_parsing_and_jit_internals.md` §3.
- **AOT Cache** (`-o` mode): No `__aiml_cache` used. Import objects are stored in the build
  directory (same as the main `.o` file). The build system (exodus) handles incremental
  compilation via file timestamps.
- **Dependency Tracking**: JIT mode uses content hashes (spec/37 §3); AOT mode relies on exodus timestamps.

## 6. Scopes and Module-Level State

### 6.1 Scope Hierarchy

AIML has three scope levels:

| Scope | Lifetime | Visible to |
|-------|----------|------------|
| **Module-level** (top-level `decl:`) | Program lifetime | All functions in the same file |
| **Function-level** (inside `def:`) | Function call | That function only |
| **Block-level** (inside `if:/while:/for:`) | Block execution | That block only |

Inner scopes may not shadow outer names. Redeclaring a visible name in an inner
scope is a `CompilationError`.

### 6.2 Module-Level Declarations in Imported Files

When a file is imported, it is compiled as a **library** (separate `.o`).
Top-level `decl:` statements in the imported file become **module-level globals**:

```yaml
# utils/state.aiml
- namespace: state

- decl: [counter, i32]        # zero-initialized global

- def:
    name: inc
    body:
      - set: [counter, { +: [counter, 1] }]

- def:
    name: get
    return: i32
    body:
      - return: counter
```

```yaml
# main.aiml
- import: "utils/state.aiml"

- def:
    name: main
    body:
      - call: [state.inc]
      - call: [state.inc]
      - print: "${ { call: [state.get] } }"   # prints 2
```

**Rules:**

1. Module-level globals in libraries are **zero-initialized** at program start
   (integers = 0, pointers/strings = null).
2. Static initializers (`decl: [x, string, "value"]`) in imported files are
   **not guaranteed to execute** before the importer's `main`. Use explicit
   setter functions to configure library state.
3. Module-level globals have **internal linkage** — they are private to their
   compilation unit and do not collide with globals of the same name in other files.
4. Functions within the same file can freely read and write module-level globals.

### 6.3 Variable Shadowing

```yaml
- decl: [x, i32, 10]

- def:
    name: example
    body:
      - print: "${x}"       # 10 (module-level)
      # - decl: [x, i32, 20]
      # -> CompilationError: shadowing is not allowed
```

## 7. Integrity and Consistency

The JIT import cache never serves an object whose inputs changed and never serves the
result of a failed compile; see `37_parsing_and_jit_internals.md` §3.
