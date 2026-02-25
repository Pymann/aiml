# Security Model

```yaml
# 1. Memory Safety (Default)
# - Array bounds checking is ON by default.
# - Null pointer checks are ON (runtime panic or result type).

- decl: [arr, i32[3]]
# - set: [arr[5], 10] -> Runtime Error (Panic) safe shutdown.

# 2. Unsafe Blocks
# Required for:
# - Raw Pointer Arithmetic
# - unchecked casting
# - disabling bounds checks
# - FFI calls (potentially)

- unsafe:
    body:
       # Raw pointer magic
       - decl: [p, ptr<i32>]
       - set: [p, { cast: [0xB8000, ptr<i32>] }] # VGA Memory?
       - set: [p + 1, 0xFF]

# 3. Capability-based Security (Future)
# Restrict what a module can do via config.

# - config:
#     capabilities: [network, fs_read]
# If a script tries 'fs_write', it fails.
```
