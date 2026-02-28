# AST Parsing Strategy (DOM vs Streaming)

The AIML Compiler relies on `yaml-cpp` to read `.aiml` files. During the architectural design of **Phase 4: AST Intermediary**, two approaches for parsing YAML into the internal Abstract Syntax Tree (`AST::Node` classes) were evaluated.

## 1. The DOM Approach (Current Implementation)
**Flow**: `YAML File` -> `YAML::LoadFile` -> `YAML::Node` Tree (RAM) -> `AST::Parser` -> `AST::Node` Tree

In this approach, the `yaml-cpp` library first reads the entire document into a generic, untyped Document Object Model (DOM) represented by `YAML::Node`. Our custom `AST::Parser` then iterates over this RAM-based tree and maps the generic nodes into type-safe C++ classes (like `CallExpr`, `DeclStmt`).

### Pros:
- **Simplicity**: Extremely easy to write and debug. The context of any node (parents, siblings, children) is always instantly accessible.
- **Lookahead**: We can easily check if a sibling node exists (e.g., checking if `modifiers` exist next to a `value`) without complex state machines.
- **Error Reporting**: `YAML::Node` stores line and column numbers naturally, making it easy to attach `Mark()` data to our `AST::Node`s for precise compiler errors.

### Cons:
- **Memory & Time Overhead**: The tree is built twice in memory (once as `YAML::Node`, once as `AST::Node`).

---

## 2. The Streaming / SAX Approach (Deferred)
**Flow**: `YAML File` -> `YAML::Parser` + `YAML::EventHandler` -> `AST::Node` Tree

Instead of building a RAM model, we would subclass `YAML::EventHandler`. As `yaml-cpp` parses the file byte-by-byte, it fires events like `OnMapStart()`, `OnScalar("call")`, `OnSequenceStart()`. A custom `AIMLBuilder` state machine would listen to these events and construct the `AST::Node`s in real-time.

### Pros:
- **Maximum Performance**: True single-pass parsing. The generic `YAML::Node` tree is never built, theoretically saving memory and shaving off a few milliseconds of compile time per file.

### Cons:
- **Extreme Complexity**: Event-driven parsing of hierarchical data requires a massive, complex internal `Stack` of states. When the parser reads the scalar `"call"`, it has no context of what that means until it checks the stack to see if it's currently inside the `body` of a `def` block or part of an `if` condition. 
- **Maintenance Nightmare**: Adding new language features (like `class` or `model` structures) would require carefully wiring new state transitions into the EventHandler, rather than just adding a simple `if (node["class"])` block.

---

## Conclusion

For **AIML v1**, the compiler prioritizes **maintainability and development speed**. 
The performance overhead of the two-pass DOM approach is negligible (typically 1-2 milliseconds per file). The vast majority of compilation time is spent in the LLVM IR generation and linking phases, not in YAML parsing.

The Streaming/Event-Driven parser is documented here as a potential micro-optimization for the future, should the AIML compiler ever need to parse millions of lines of code where millisecond-level constraints become a bottleneck.
