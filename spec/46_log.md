# Spec 46 — `log` Namespace

## Overview

Structured, leveled logging with automatic source-location injection.
Output goes to **stderr** by default, optionally mirrored to a log file.

## Levels

| Priority | Name         | Filterable | Description                           |
|----------|--------------|------------|---------------------------------------|
| 0        | `verbose`    | yes        | Very detailed, below debug            |
| 1        | `debug`      | yes        | Developer diagnostics                 |
| 2        | `info`       | yes        | Normal operation (default threshold)  |
| 3        | `warn`       | yes        | Potential problems                    |
| 4        | `error`      | yes        | Errors, execution continues           |
| 5        | `fatal`      | yes        | Errors, calls `exit(1)` after logging |
| —        | `deprecated` | **no**     | Always printed, marks obsolete usage  |
| —        | `system`     | **no**     | Always printed, runtime diagnostics   |

`log.set_level("debug")` sets the minimum threshold for filterable levels.
Messages below the threshold are discarded. Default threshold: **info** (2).

`deprecated` and `system` bypass the threshold entirely — they are always emitted.

## Output Format

```
2026-03-13 14:22:01 [INFO]       server started on port 8080    (backend/main.aiml:41)
2026-03-13 14:22:01 [VERBOSE]    pool details: size=128          (backend/pool.aiml:18)
2026-03-13 14:22:02 [DEPRECATED] use ws.connect instead          (client/net.aiml:55)
2026-03-13 14:22:02 [SYSTEM]     gc cycle completed in 3ms       (runtime)
```

Format: `<timestamp> [<LEVEL>] <padding> <message>  (<file>:<line>)`

- Timestamp: `YYYY-MM-DD HH:MM:SS` (local time)
- Level tag: upper-case, left-aligned in bracket
- Message: user-provided string (supports `${var}` interpolation as usual)
- Location: relative source file path + line number, injected by compiler

### Color (stderr only, when isatty)

| Level      | Color          |
|------------|----------------|
| verbose    | dark gray      |
| debug      | cyan           |
| info       | green          |
| warn       | yellow         |
| error      | red            |
| fatal      | red bold       |
| deprecated | magenta        |
| system     | blue           |

No color codes are written to log files.

## API

### Logging Functions

```yaml
- call: [log.verbose,    "pool internals: ${detail}"]
- call: [log.debug,      "fd=${fd} state=${state}"]
- call: [log.info,       "server started on port ${port}"]
- call: [log.warn,       "retry attempt ${n}"]
- call: [log.error,      "bind failed: port in use"]
- call: [log.fatal,      "unrecoverable: out of memory"]   # logs then exit(1)

- call: [log.deprecated, "use ws.connect instead of net.raw_ws"]
- call: [log.system,     "gc cycle completed in 3ms"]
```

All functions take a single `string` argument (the message).

### Configuration

```yaml
- call: [log.set_level, "debug"]        # set threshold: verbose/debug/info/warn/error/fatal
- call: [log.set_file,  "server.log"]   # mirror output to file (append mode)
```

`log.set_level` only affects filterable levels (verbose..fatal).
`log.set_file` opens the file in append mode. Subsequent calls replace the file.

## Compiler Integration — Source Location Injection

The compiler transforms every `log.*` call by appending a hidden location argument:

```
User writes:     call: [log.info, "started"]
Compiler emits:  aiml_log_info("started", "backend/main.aiml:41")
```

The location string is a compile-time constant: `"<relative_source_path>:<line_number>"`.
The path is relative to the project `source_root`.

For `log.system` calls emitted by the runtime itself (not from user AIML code),
the location is the literal string `"runtime"`.

## C Runtime Interface

```c
void aiml_log_verbose(const char* msg, const char* loc);
void aiml_log_debug(const char* msg, const char* loc);
void aiml_log_info(const char* msg, const char* loc);
void aiml_log_warn(const char* msg, const char* loc);
void aiml_log_error(const char* msg, const char* loc);
void aiml_log_fatal(const char* msg, const char* loc);    // calls exit(1)
void aiml_log_deprecated(const char* msg, const char* loc);
void aiml_log_system(const char* msg, const char* loc);

void aiml_log_set_level(const char* level);    // "verbose"/"debug"/"info"/"warn"/"error"/"fatal"
void aiml_log_set_file(const char* path);      // NULL or "" to disable file output
```

## Thread Safety

All log functions are thread-safe (single mutex for output serialization).
`set_level` and `set_file` are also mutex-protected.
