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

# 3. System Environment
# Exit Progam
- call: [sys.exit, 1]

# Command Line Arguments
# Access logic to argv/argc
- decl: [first_arg, string]
- set: [first_arg, { call: [sys.get_arg, 1] }]

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

## Process State Semantics

- `process.returncode` is nullable (`i32?`).
- While the process is still running, `returncode == null`.
- If process creation itself fails in an uncaught way, normal exception
  propagation rules apply and an unhandled failure exits the program with
  `sys.exit(2)`.
