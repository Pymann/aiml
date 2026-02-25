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
