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

### 4.2 Binary Compilation (`aiml main.aiml -o output.o`)
- **Behavior**: Separate Compilation.
- **Imports**:
    1. Checks if imported file needs recompilation (missing or older `.o`).
    2. Compiles imported file to a native object file if needed.
    3. Main file compiled to `.o`.
- **Linking**: All object files linked by the system linker (gcc/clang).

## 5. Paths and Caching

- **Import Paths**: Relative to the directory of the importing file.
    - `import: "src/utils/math.aiml"` → resolved from the importing file's directory.
- **Cache Directory**: `__aiml_cache/` in the CWD (or `$AIML_CACHE` if set).
    - Directory structure mirrored to avoid collisions.
    - Source: `src/utils/math.aiml` → Object: `__aiml_cache/src/utils/math.o`
- **Dependency Tracking**: Recompiles if source is newer than cached object.

## 6. Integrity and Consistency

- **Manifest File**: `__aiml_cache/compilations.yaml`
- **Content**: Mapping of source file names to SHA512 hashes.
- **Process**:
    1. Before compiling, SHA512 of source file is computed.
    2. Checked against `compilations.yaml`.
    3. If hash differs from stored entry → compilation **aborts** with an error.
    4. On success, new hash is written to manifest.
- File names must be globally unique regardless of folder location.
