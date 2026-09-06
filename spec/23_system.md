# System & Standard Library Proposal

```yaml
# 1. File I/O
# Maps to fopen/fwrite/fread/fclose (C Std Lib)

# Standard Streams (Pre-defined Handles)
# - io.stdout
# - io.stderr
# - io.stdin

# Writing to Stdout / Stderr
- call: [io.write, io.stdout, "Normal Output\n"]
- call: [io.write, io.stderr, "Critical Error!\n"]

# Reading from Stdin
- decl: [input, string]
# Read 100 bytes or until EOF/Newline
- set: [input, { call: [io.read, io.stdin, 100] }]

# File Operations
# Writing
- decl: [f, file_handle]
- set: [f, { call: [io.open, "test.txt", "w"] }]
- if:
    condition: { eq: [f, null] }
    then:
      - call: [io.write, io.stderr, "open failed\n"]
      - call: [sys.exit, 2]

- call: [io.write, f, "Hello World\n"]
- call: [io.close, f]

# Reading
- set: [f, { call: [io.open, "test.txt", "r"] }]
- decl: [content, string]
- set: [content, { call: [io.read_all, f] }] # Convenience helper
- print: [content]
- call: [io.close, f]

# 2. Time & Date
# Maps to time()/clock_gettime()

# Timestamp (Unix Epoch in seconds or ms)
- decl: [ts, i64]
- set: [ts, { call: [time.now] }]
- print: [ts]

# Sleep (Execution Pause)
- call: [time.sleep_ms, 1000] # Sleep 1s

# Format an epoch-ms timestamp as "YYYY-MM-DD HH:MM" (UTC).  Returns a string.
# Reusable for any i64 millisecond timestamp (e.g. time.now).  Works natively
# and in the WASM build (gmtime_r/strftime under the hood).
- decl: [when, string]
- set: [when, { call: [time.format_utc, ts] }]   # e.g. "2026-09-02 18:07"

# 3. System Environment
# Exit Progam
- call: [sys.exit, 1]

# Command Line Arguments
# Access logic to argv/argc
- decl: [first_arg, string]
- set: [first_arg, { call: [sys.get_arg, 1] }]

# sys.arg_dump
# Parses CLI flags directly into an existing struct/model variable.
# Signature: sys.arg_dump(target: ModelOrStruct) -> void
#
# Supported field types in the current implementation:
#   i32     - consumes next token as atoi(value)
#             if omitted or followed by another flag, sets field to 1
#   f32     - consumes next token as atof(value)
#   string  - consumes next token as strdup(value)
#   i64     - consumes next token as atoll(value)
#
# Flag-name mapping:
#   --foo-bar  -> foo_bar
#   --foo      -> foo
#
# Notes:
# - argv[0] is ignored; parsing starts at argv[1].
# - Positional arguments (tokens not starting with '-') are silently ignored.
# - Unknown flags print an error and terminate the process with exit code 1.
# - The target must be a named struct/model variable, not an arbitrary expression.
#
# Example:
- model:
    name: TrainCfg
    fields:
      - [config, string, ""]
      - [epochs, i32, 10]
      - [lr, f32, 0.001]
      - [seed, i64, 0]

- decl: [cfg, TrainCfg]
- call: [sys.arg_dump, cfg]
#
# Example invocation:
#   ./prog --config train.json --epochs 20 --lr 0.0005 --seed 42
# Result:
#   cfg.config = "train.json"
#   cfg.epochs = 20
#   cfg.lr = 0.0005
#   cfg.seed = 42

# 4. Size and Type
- decl: [any_class, AnyClass]
- decl: [size, u64]
- set: [size, { call: [sys.size, AnyClass] }]
- decl: [size2, u64]
- set: [size2, { call: [sys.size, {call: [sys.type, any_class]}] }]
- print: ["${size} == ${size2}"]


# 5. Process Management

# Process Configuration Model
# Equivalent to subprocess.run kwargs
- model:
    name: sys.model.ProcessOptions
    fields:
      - [cwd, string, ""]             # Working directory
      - [env, map<string:string>, {}] # Environment variables (empty = inherit)
      - [shell, bool, false]          # Run via shell
      - [timeout, i32, -1]            # Timeout in ms (-1 = infinite)
      - [capture_output, bool, false] # Capture stdout/stderr in buffer

# sys.env
# Access to environment variables as a map
# Type: map<string, string>
- decl: [current_env, map<string, string>]
- set: [current_env, sys.env]

# process Type
# Represents a system process, modeled after Python's subprocess.Popen
# Members:
#   args: list<string>      - Command and arguments used to spawn
#   cwd: string             - Working directory
#   env: map<string, string>- Environment variables
#   pid: i64                - Process ID
#   returncode: i32?        - Exit status; null while still running
#   stdout_buffer: string   - Captured standard output
#   stderr_buffer: string   - Captured standard error
#   shell: bool             - Whether shell execution was used (default false)
#
# Methods:
#   start() -> void         - Starts the process (if not auto-started)
#   wait() -> i32           - Blocks until process finishes, returns exit code
#   kill() -> void          - Terminates the process
#   read_line() -> string   - Reads a line from stdout (blocking)
#   read_err_line() -> string - Reads a line from stderr (blocking)
#   eof() -> bool           - Returns true if process has finished and buffers are empty

# sys.exe
# Spawns a new process
# Signature: sys.exe(argv: list<string>, options: sys.model.ProcessOptions) -> process
- def:
    name: sys.exe
    args: [[argv, list<string>], [options, sys.model.ProcessOptions]]
    return: process

# Example Usage
- decl: [args, list<string>]
- set: [args, ["ls", "-la"]]

# Option 1: Using Model Instance
- decl: [opts, sys.model.ProcessOptions]
- set: [opts, { new: sys.model.ProcessOptions }]
- set: [opts.cwd, "/tmp"]
- decl: [proc, process]
- set: [proc, { call: [sys.exe, args, opts] }]

# Option 2: Using Map (Structural Typing)
- set: [proc, { call: [sys.exe, args, {cwd: "/var", shell: true}] }]

# Option 3: Default Options (Empty Map)
- set: [proc, { call: [sys.exe, args, {}] }]
```

## 6. Memory Management

```yaml
# sys.mem_limit(bytes: i64)
# Sets a virtual memory limit (RLIMIT_AS) for the entire process.
# When the limit is exceeded, malloc fails and the process is terminated
# with a diagnostic instead of freezing the machine (OOM/swap thrashing).
# Pass 0 to remove the limit.
- decl: [limit, i64, 4096]
- set: [limit, { "*": [limit, 1048576] }]   # 4 GB
- call: [sys.mem_limit, limit]

# sys.mem_usage() -> i64
# Returns the current RSS (Resident Set Size) in bytes.
# Useful for monitoring memory consumption at runtime.
- decl: [rss, i64, { call: [sys.mem_usage] }]
- decl: [rss_mb, i64, { /: [rss, 1048576] }]
- print: ["Current memory: ${rss_mb} MB"]

# Remove limit
- call: [sys.mem_limit, 0]
```

### Platform Notes

- `sys.mem_limit` uses `setrlimit(RLIMIT_AS)` on Linux. Sets the soft limit
  only; hard limit is preserved so the limit can be removed later.
- `sys.mem_usage` reads `/proc/self/statm` on Linux (RSS in pages × page size).
- On unsupported platforms, `mem_limit` prints a warning and `mem_usage` returns 0.

## File I/O Failure Semantics

- `io.open(...)` returns `null` on failure.
- Callers are expected to check the returned handle before `io.read`, `io.write`
  or `io.close`.

## Proposed Binary File I/O Extensions (Not Implemented Yet)

For large binary model imports, the current text-oriented `io.open/io.read/io.read_all`
API is not sufficient. The following additions are proposed.

Status: **proposed, not implemented**

```yaml
# Binary open
- decl: [f, file_handle]
- set: [f, { call: [io.open_binary, "model.bin"] }]

# Sequential byte reads
- decl: [hdr, buffer<u8>, { call: [io.read_bytes, f, 64] }]

# Position control
- decl: [pos, i64, { call: [io.tell, f] }]
- call: [io.seek, f, 1024]
- call: [io.skip, f, 256]

# Parallel-friendly range reads
- decl: [block, buffer<u8>, { call: [io.read_at, f, 1048576, 65536] }]

# Typed helpers for small scalar reads
- decl: [flag, i32, { call: [io.read_bool, f] }]
- decl: [magic, i32, { call: [io.read_i32_le, f] }]
- decl: [count, i64, { call: [io.read_i64_le, f] }]
- decl: [v, f32, { call: [io.read_f32_le, f] }]
- decl: [t, f64, { call: [io.read_f64_le, f] }]
```

### Proposed Functions

| Function | Signature | Status | Description |
|----------|-----------|--------|-------------|
| `io.open_binary` | `io.open_binary(path: string\|path) -> file_handle` | not implemented | Opens a file in binary read mode. |
| `io.read_bytes` | `io.read_bytes(f: file_handle, n: i32) -> buffer<u8>` | not implemented | Reads exactly `n` bytes or fewer at EOF into a byte buffer. |
| `io.tell` | `io.tell(f: file_handle) -> i64` | not implemented | Returns current file position in bytes. |
| `io.seek` | `io.seek(f: file_handle, pos: i64) -> void` | not implemented | Moves the file position to `pos`. |
| `io.skip` | `io.skip(f: file_handle, delta: i64) -> void` | not implemented | Moves the file position forward by `delta`. |
| `io.read_at` | `io.read_at(f: file_handle, offset: i64, n: i32) -> buffer<u8>` | not implemented | Reads a byte range without relying on the shared sequential cursor. |
| `io.read_bool` | `io.read_bool(f: file_handle) -> i32` | not implemented | Reads one boolean byte and advances the cursor. |
| `io.read_i32_le` | `io.read_i32_le(f: file_handle) -> i32` | not implemented | Reads one little-endian `i32` and advances the cursor. |
| `io.read_u32_le` | `io.read_u32_le(f: file_handle) -> i64` | not implemented | Reads one little-endian unsigned `u32`. |
| `io.read_i64_le` | `io.read_i64_le(f: file_handle) -> i64` | not implemented | Reads one little-endian `i64`. |
| `io.read_u64_le` | `io.read_u64_le(f: file_handle) -> i64` | not implemented | Reads one little-endian unsigned `u64`. |
| `io.read_f32_le` | `io.read_f32_le(f: file_handle) -> f32` | not implemented | Reads one IEEE754 `f32`. |
| `io.read_f64_le` | `io.read_f64_le(f: file_handle) -> f64` | not implemented | Reads one IEEE754 `f64`. |

### Design Notes

- `io` owns file and stream access.
- `buffer` owns in-memory interpretation of bytes.
- `io.read_at` is especially important for parallel import of very large files
  (for example, multiple workers reading distinct matrix ranges from the same file).

## Process State Semantics

- `process.returncode` is nullable (`i32?`).
- While the process is still running, `returncode == null`.
- If process creation itself fails in an uncaught way, normal exception
  propagation rules apply and an unhandled failure exits the program with
  `sys.exit(2)`.
