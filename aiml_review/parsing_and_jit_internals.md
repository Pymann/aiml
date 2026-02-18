# AIML Compiler Challenges: Parsing and JIT Internals

This document explains two critical categories of challenges faced during the AIML compiler development and how they were resolved.

## 1. Syntax Conflicts with YAML

Because AIML uses YAML as its structural syntax, we face unique parsing challenges when embedding complex expressions or types that clash with YAML's own special characters.

### The Map Type Problem (`map<K,V>`)
YAML uses commas `,` as delimiters in flow-style collections (e.g., `[a, b]`).
- **Conflict**: A declaration like `map<string, i32>` contains a comma.
- **Issue**: YAML parsers might interpret `map<string` as one value and ` i32>` as another, or simply fail to parse the string correctly without heavy quoting.
- **Solution**: We enforced a strict syntax `map<Key:Value>` using a colon instead of a comma. This avoids the comma conflict entirely.

### String Expansion and Flow Maps
AIML allows dynamic string interpolation using `${...}`.
- **Scenario**: Printing a computed value from a map often requires a nested call, e.g., `${ { call: [scores.get, "Alice"] } }`.
- **Conflict**: The inner `{ ... }` syntax is standard YAML for "Flow Mappings".
- **Parsing Issue**: When extracting the content inside `${...}`, we must be careful about leading/trailing whitespace. If we pass ` { call: ... } ` (with spaces) to a YAML parser, strict parsers might reject it or misinterpret the root object.
- **Solution**: We implemented a trimming utility in `StringUtils::expandExpression` to remove whitespace before parsing the inner YAML content.

---

## 2. JIT Compilation and Execution

The AIML compiler uses LLVM's JIT (Just-In-Time) compilation to execute code immediately in memory without creating an executable file. This requires specific runtime configurations.

### Target Registration (`InitializeAllTargets`)
LLVM is a modular infrastructure that supports many architectures (x86, ARM, RISC-V, etc.).
- **Issue**: By default, LLVM does not initialize any backends. When we try to compile IR to machine code, we get an error: *"Unable to find target for this triple"*.
- **Fix**: We must explicitly call `llvm::InitializeAllTargets()` (and related functions) to register the code generators linked into our binary. We also set the module's "Target Triple" to match the host machine (e.g., `x86_64-pc-linux-gnu`) so the JIT knows which instruction set to use.

### Dynamic Symbol Loading (`LoadLibraryPermanently`)
The generated AIML code often calls standard C functions like `printf`, `malloc`, or `exit`.
- **Runtime Error**: In a standard compiled binary, the linker resolves these. In JIT mode, the generated machine code is executed within the *compiler's own process*.
- **Issue**: If the JIT engine doesn't know where `printf` is located in memory, it cannot link the call, leading to a crash or lookup failure.
- **Fix**: We call `llvm::sys::DynamicLibrary::LoadLibraryPermanently(nullptr)`. This tells LLVM to "look in the current process" for any unresolved symbols. This effectively exposes the compiler's own linked C standard library to the JIT-executed code.
