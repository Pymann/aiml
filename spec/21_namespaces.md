# Modules and Namespaces

## 1. Defining a Namespace
The `namespace` command sets the namespace for the **entire file**.
It is typically the first command in a file. Namespaces with *sys* at start will be forbidden and cause compilation error: InternalSysNamespaceOverload

```yaml
# sets namespace for all subsequent definitions in this file
- namespace: my.math

- def:
    name: calc  # Becomes "my.math.calc"
    ...
```

## 2. Importing Modules
The `import` command loads another AIML file.
- The compiler ensures each file is **compiled only once**, even if imported multiple times.
- Imported symbols must be accessed via their **Fully Qualified Name (FQN)**.

```yaml
- import: "utils/math.aiml"

- def:
    name: main
    body:
      # Must use full name defined in math.aiml
      - call: [my.math.calc]
```

## 4. Execution vs Compilation

The `import` command behaves intelligently based on how the code is run.

### 4.1 Direct Execution (`aiml main.aiml -r` or `aiml main.aiml --run`)
- **Behavior**: JIT (Just-In-Time) compilation with Object Caching.
- **Imports**: 
    1. Imported files are checked against valid cache in `__aiml_cache`.
    2. If missing or outdated, they are compiled to native object files (`.o`).
    3. The main file is compiled to LLVM IR in memory.
    4. The imported object files are explicitly loaded into the JIT execution engine (using `LoadLibraryPermanently` or `addObjectFile`).
- **Result**: Hybrid execution where dependencies are pre-compiled artifacts, and the main script is JIT-compiled and linked against them.

### 4.2 Binary Compilation (`aiml main.aiml -o output.o`)
- **Behavior**: Separate Compilation.
- **Imports**: 
    1. The compiler checks if the imported file needs compilation (e.g., `lib.o` missing or older than `lib.aiml`).
    2. If needed, it compiles the imported file to a native object file (`.o`).
    3. The main file is compiled to `.o` (using "Header-Only" mode for imports to check types).
- **Linking**: Finally, all object files are linked together by the system linker (gcc/clang) to create the executable.
- **Result**: A strict, efficient binary build process.

## 5. Paths and Caching

- **Import Paths**: All import paths are relative to the Current Working Directory (CWD) of the compiler invocation.
    - `import: "src/utils/math.aiml"` -> `./src/utils/math.aiml`

- **Cache Directory**: Compiled object files are stored in a not-hidden `__aiml_cache` directory in the CWD if environmental variable `AIML_CACHE` is not set. 
    - The directory structure is mirrored to avoid collisions.
    - Source: `src/utils/math.aiml`
    - Object: `__aiml_cache/src/utils/math.o`

- **Dependency Tracking**: The compiler automatically recompiles files if the source is newer than the cached object file.

## 6. Integrity and Consistency

To ensure that the build is consistent (i.e., source files do not change *during* a long compilation process or between link steps), the compiler maintains a manifest.

- **Manifest File**: `__aiml_cache/compilations.yaml`
- **Content**: A mapping of source file-names only to their SHA512 hashes.
- **Process**:
    1. Before compiling or importing any file, the compiler calculates the SHA512 hash of the source file.
    2. It checks this hash against the entry in `compilations.yaml`.
    3. If an entry exists and the hash differs, the compilation **aborts immediately** with an error, also informing user, that redundant filenames, no matter of folder-location is not applicable This prevents linking against outdated or changing code.
    4. If no entry exists (or after successful compilation), the new hash is written to the manifest.

