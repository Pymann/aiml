# AIML Configuration Directives

```yaml
# Metadata to configure the Runtime / Compiler Environment.
# Usually at the top of the file.

- config:
    # Desired Stack Size (in Bytes)
    # On Linux: can map to setrlimit(RLIMIT_STACK, ...) or linker flags.
    stack_size: 1048576 # 1 MB
    
    # Initial Heap Size (for custom allocators or WASM)
    heap_size:  16777216 # 16 MB
    
    # Target Architecture (optional check)
    target: "x86_64"
    
    # Version Requirement
    version: "1.0"

- def:
    name: main
    args: []
    return: i32
    body:
       - print: ["Config loaded"]
       - return: [0]
```
