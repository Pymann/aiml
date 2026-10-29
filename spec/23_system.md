# System & Standard Library Proposal

```yaml
# 1. File I/O
# Maps to fopen/fwrite/fread/fclose (C Std Lib)

# Standard Streams (Pre-defined Handles)
# - io.stdout
# - io.stderr
# - io.stdin
# Each is the process's own C standard stream (the same handle on every access; stderr keeps
# its C buffering, i.e. unbuffered). They are runtime-owned: never closed by scope exit.

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

# Monotonic milliseconds (never jumps with the wall clock) — for timeouts and deadlines
- decl: [deadline, i64, { +: [{ call: [time.mono_ms] }, 5000] }]

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

### Es gibt kein `exit:`-Statement

Das Programm endet mit `call: [sys.exit, N]` — und **nur** damit.  Ein Statement
`- exit: [1]` ist KEIN gueltiges aiml; der Compiler weist es seit 2026-10-15 als
unbekannten Statement-Schluessel ab (spec/49 §49.x).

Das steht hier, weil es teuer war: `- exit: [1]` wurde vom Statement-Builder
still verworfen (unbekannter Schluessel → `nullptr`, kein Wort).  54 Stellen im
Testkorpus bauten darauf — jeder FAIL-Zweig endete mit `- exit: [1]`, lief
durch und lieferte Rueckgabewert **0**.  Die Suite meldete also PASS fuer Tests,
die „FAIL" druckten.  Ein zweiter Weg zum selben Ziel waere ueberfluessig
gewesen; was fehlte, war die Fehlermeldung.

# Command Line Arguments
# Access logic to argv/argc
- decl: [first_arg, string]
- set: [first_arg, { call: [sys.get_arg, 1] }]
#
# JIT: `sys.get_arg` returns "" for EVERY index. `main(argc, argv)` writes the `__argv`/`__argc`
# globals (compiler.cpp, AOT path only); in a `aiml -r` run nothing writes them, `__argv` stays
# null and the safety branch in SysGetArg yields the empty string. A program that reads its own
# arguments therefore behaves differently under `aiml -r` than as a built binary — measured
# 16.10.2026, previously undocumented.

# Absolute path of the running executable (Linux: /proc/self/exe), "" when unknown
# (web, JIT runs report the compiler binary). Runtime-owned like sys.get_arg — for locating
# assets installed next to the program, independent of the working directory.
- decl: [exe, string, { call: [sys.exe_path] }]

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


# sys.env

> ✅ **Property-Form gefixt (2026-10-21):** `sys.env` ist hier als PROPERTY
> dokumentiert, kompilierte aber zu einem NULLPOINTER (`ast_compiler_expr.cpp`),
> waehrend nur die Call-Form `{ call: [sys.env] }` echte Daten lieferte. Folge:
> `- set: [m, sys.env]` lief durch, der erste Lesezugriff (`.len`/`.contains`/
> `.get`) dereferenzierte null → SIGSEGV. Beide Formen emittieren jetzt denselben
> `aiml_sys_env`-Call; `tests/test_sys_env.aiml` prueft beide.
>
> ⚠️ **Lebensdauer (2026-10-11):** Ein mit `sys.env` erzeugtes `map<string:string>` ist ein
> gewöhnliches Local. Ein Wert aus `envm.get(key)` ist **nur so lange gültig, wie die Map
> lebt** — die in spec/61 zugesagte `incref` für String-Werte findet derzeit nicht statt.
> Wer den Wert länger braucht (Rückgabe, Modellfeld), muss ihn kopieren, z. B. über
> Interpolation: `- return: "${raw}"`. Siehe spec/61, Abschnitt Container-Lesezugriff.
# Access to environment variables as a map
# Type: map<string:string>  (der Compiler akzeptiert NUR die Doppelpunkt-Form;
# `map<string, string>` ist ein Uebersetzungsfehler)
- decl: [current_env, map<string:string>]
- set: [current_env, sys.env]
```


## 4.1 Host parallelism — `core_count`, `logical_thread_count`, `quota_thread_count`

Three separate questions, three functions. Sizing a thread pool from the wrong
one either leaves cores idle or over-subscribes a container.

```yaml
# Signatures (call-form only, all -> i32, all >= 1):
- decl: [cores,   i32, { call: [sys.core_count] }]            # physical cores, no SMT
- decl: [threads, i32, { call: [sys.logical_thread_count] }]  # logical processors (SMT included)
- decl: [budget,  i32, { call: [sys.quota_thread_count] }]    # what a cgroup quota actually allows
```

| Function | Answers | Source |
|----------|---------|--------|
| `sys.core_count` | How many *independent* cores exist? Use for CPU-bound pools where SMT siblings only add contention. | Topology (`core_id`/`package_id` pairs, deduplicated); falls back to `logical_thread_count` when topology is unreadable |
| `sys.logical_thread_count` | How many threads can run at once? Honours the process **affinity mask**, so `taskset`/`cpuset` is respected. | `sched_getaffinity`, else `std::thread::hardware_concurrency` |
| `sys.quota_thread_count` | How many threads' worth of CPU time may this process actually consume? | cgroup v2 `cpu.max`, else cgroup v1 `cpu.cfs_quota_us`/`cpu.cfs_period_us`; **unlimited → equals `logical_thread_count`** |

Normative:

- All three are **never** less than `1`. If the OS reports nothing usable they
  return `1` instead of `0`, so a sizing formula always yields at least one
  worker and `n / sys.core_count()` can never divide by zero.
- All three are **call-form only**. There is deliberately no property form — the
  mistake behind the `sys.env` SIGSEGV above (property documented, null pointer
  emitted) is not repeated here.
- `quota_thread_count` rounds **down** to whole threads but never below 1: a
  `--cpus=0.5` container reports `1`, not `0`.
- Pure/stateless → Category 2 in chapter 47: callable from any thread without
  synchronisation. Values are read once and cached per process, so repeated
  calls are cheap and return the same answer.
- A CPU quota does not shrink the affinity mask: a container pinned to 2 CPUs'
  worth of quota on a 16-thread host reports `logical_thread_count == 16` and
  `quota_thread_count == 2`. **The effective cap for a thread pool is therefore
  `quota_thread_count`**, which already collapses to the logical count when no
  quota is set. See chapter 40 (containerization).

Intended use — sizing a worker pool (chapter 26):

```yaml
# One worker per 4 units of work, capped by what the host really grants:
- decl: [want, i32, { /: [{ +: [units, 3] }, 4] }]            # ceil(units/4)
- decl: [cap,  i32, { call: [sys.quota_thread_count] }]
- decl: [workers, i32, want]
- if: { condition: { gt: [want, cap] }, then: [{ set: [workers, cap] }] }
- if: { condition: { lt: [workers, 1] }, then: [{ set: [workers, 1] }] }
```

## 5. Process Management

### 5.1 `sys.exe` — spawning

```yaml
# Signature: sys.exe(argv: list<string> [, options]) -> process
- decl: [argv, list<string>, ["ls", "-la"]]
- decl: [proc, process, { call: [sys.exe, argv] }]
- decl: [proc2, process, { call: [sys.exe, argv, {cwd: "/tmp", capture: true, timeout: 5000}] }]
```

**Ein falsch typisiertes erstes Argument ist ein Uebersetzungsfehler** (seit 17.10.2026). Bis
dahin war es ein **SIGSEGV**: ein String ist in LLVM ebenfalls ein `i8*`, rutschte darum durch
die Zeiger-Weiche des Builtins und wurde von der Laufzeit als Listenkopf gelesen. Geprueft wird,
was statisch bekannt ist — ein Literal ist nie eine Liste, und bei einer Variablen steht der
deklarierte Typ im Symbol; ein zusammengesetzter Ausdruck bleibt ungeprueft (lieber keine
Diagnose als eine falsche). Negativprobe: `tests/test_sys_exe_argv_type_neg.aiml`.

`argv` is a `list<string>` variable (list literals exist only as `decl` initializers, spec/10).
`argv[0]` is looked up in `PATH`. The process is started immediately. A spawn that fails
(program not found, `cwd` missing, unreadable redirect target) returns a `null` process.

`options` is an **option bag** — a map literal written at the call site (like `db.open_kv`),
not a model. `bool` options must be literals (they are resolved at compile time); all others
are expressions. Unknown keys are a compile error. All keys are optional:

> **To be changed for `spec/62` F62-27 — `ExeOptions` becomes a built-in model.**  `spec/62`
> needs these options as a reusable value: a pipeline builds one exec description per lane, hands
> it around and stores it beside its step.  A map literal that exists only at a call site cannot
> do that.
>
> The literal rule for `shell`/`capture`/`detach` protects nothing.  Measured in the compiler:
> the three are stored as C++ `bool` in `ctx.exeOpts` (`context.h`) while the other six fields are
> `llvm::Value*`, and at the call they are emitted as `getInt32(o.shell ? 1 : 0)` — so
> `aiml_process_spawn` already receives them as ordinary runtime integers.  The restriction is an
> artefact of the compiler's storage, not of the ABI.
>
> Lifting it therefore costs exactly one thing: the `capture: true` + `detach: true`
> incompatibility is checked at compile time today (`ast_compiler_call.cpp`) and has to move into
> the runtime, which returns a `null` process and logs the reason.  That is the known price, and it
> is the usual one for turning a compile-time-only form into a value.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `cwd` | `string` | `""` | Working directory (`""` = inherit). |
| `env` | `map<string:string>` | inherit | Complete environment of the child (replaces, does not extend — take `sys.env` and `put` to extend). |
| `shell` | `bool` | `false` | Run `/bin/sh -c argv[0]`; `argv[1..]` become `$0, $1, …` (as Python `shell=True`). |
| `timeout` | `i32` | `-1` | Milliseconds from spawn (`-1` = none). See §5.3. |
| `capture` | `bool` | `false` | stdout/stderr go to pipes → `stdout_buffer`, `stderr_buffer`, `read_line`, `read_err_line`. |
| `stdout` | `string` | `""` | Redirect stdout to this file (created/truncated). Takes precedence over `capture` for stdout. |
| `stderr` | `string` | `""` | Redirect stderr to this file (created/truncated). Takes precedence over `capture` for stderr. |
| `stdin` | `string` | `""` | Read stdin from this file (F23-50; `""` = inherit, a detached job always reads `/dev/null`). A file that cannot be opened makes the spawn fail (`null`). |
| `detach` | `bool` | `false` | Durable process, §5.4. |
| `status_file` | `string` | `""` | Only with `detach`: exit-status file, §5.4. |

Without redirection or capture the child inherits stdin/stdout/stderr. **The child inherits
no other descriptor:** `sys.exe` closes every descriptor `>= 3` in the child. In addition the
descriptors the runtime keeps open are close-on-exec — `io.open`/`io.open_binary` files, the
`log.set_file` target, `net`/`tls`/`ws`/`http`/`mirror` sockets, KV/store/WAL files and capture
pipes. A child therefore never keeps a parent's network connection or database file open.

### 5.2 The `process` type

`process` is a Class-4 unique resource (spec/61 §2.1): move-only, released at owner drop.
The drop closes the capture pipes and frees the handle; it neither waits for nor kills the
child (a running child keeps running; the runtime reaps it later so no zombie accumulates).

| Member (read-only) | Type | Meaning |
|---|---|---|
| `pid` | `i64` | Process id (for `detach`: the job's id, not the monitor's). |
| `returncode` | `i32` | Exit status, **valid once `running()` is `false`**: `0..255` = exit code, `128+N` = killed by signal `N`, `124` = killed by `timeout`, `-1` = unknown (an attached process vanished without status, §5.4). `-1` while running. |
| `stdout_buffer` | `string` | Captured stdout not consumed by `read_line` (complete after `wait()`). |
| `stderr_buffer` | `string` | Same for stderr. |

| Method | Returns | Meaning |
|---|---|---|
| `running()` | `bool` | Non-blocking status check; reaps a finished child and sets `returncode`. |
| `wait()` | `i32` | Blocks until the process ends (draining capture pipes), returns `returncode`. |
| `kill()` | `void` | `SIGKILL` (detached: its whole process group). |
| `signal(sig)` | `i32` | Sends signal number `sig` (e.g. 15 = SIGTERM, 2 = SIGINT) — to the process group for a detached / attached job; `0` sent, `-1` not (finished, no such signal) (F23-51). |
| `read_line()` | `string` | Next stdout line without the trailing `"\n"` (blocking); `""` at end of stream. |
| `read_err_line()` | `string` | Same for stderr. |
| `eof()` | `bool` | `true` when the process has ended and no captured output is left to read. |

All members and methods are null-safe (a `null` process: `running()` false, `returncode -1`,
`wait()` -1, strings `""`).

### 5.3 Timeout

With `timeout: ms`, a process still running when the deadline (spawn + `ms`) is reached while
`wait()` or `running()` observes it is sent `SIGTERM`, then `SIGKILL` after 2 s; `returncode`
becomes `124` (as GNU `timeout`). For a detached process the monitor enforces the deadline
itself, independent of the parent.

### 5.4 Durable processes — `detach`, `status_file`, `sys.process_attach`

A detached process survives its parent (agent crash, restart, lost connection — the Jenkins
durable-task pattern):

```yaml
- decl: [job_argv, list<string>, ["sh", "-c", script]]
- decl: [job, process, { call: [sys.exe, job_argv,
          {detach: true, stdout: "ws/job.log", stderr: "ws/job.log", status_file: "ws/job.status"}] }]
- decl: [job_pid, i64, job.pid]          # persist this together with the status file path
```

- The runtime forks a **monitor** in a new session; the monitor starts the job as its own
  process-group leader (`pgid == pid`), waits for it and writes `returncode` (decimal + `"\n"`)
  **atomically** (temp file + rename) to `status_file`, then exits with the same code.
- The job's stdin is `/dev/null`; stdout/stderr go to `/dev/null` unless redirected.
  `capture: true` together with `detach: true` is a compile error.
- While the parent lives, `running()`/`wait()`/`returncode` work as for any process.
- After a parent restart, re-acquire the job with
  `sys.process_attach(pid: i64, status_file: string) -> process`: `running()` is `true` while
  `status_file` does not exist and `pid` is alive; once the file exists `returncode` is read from
  it; if `pid` is gone without a status file, `running()` is `false` and `returncode` is `-1`.
  `wait()` polls; `kill()` kills the job's process group. (PID reuse between restarts is not
  detected — keep the status file next to the job's workspace.)
- Without `status_file` the exit status is only observable while the parent lives.

### 5.5 Many processes at once — `sys.wait_any` (F23-52)

```aiml
- decl: [jobs, list<process>]
# … push up to N compiler processes (capture: true) …
- while:
    condition: { gt: [jobs.len, 0] }
    body:
      - decl: [done_at, i32, { call: [sys.wait_any, jobs, -1] }]
      - if:
          condition: { lt: [done_at, 0] }      # timeout or a caught signal
          then: [{ call: [stop_all, jobs] }]
          else:
            - decl: [job, ref<process>, { at: [jobs, done_at] }]
            - print: ["rc=${job.returncode}: ${job.stderr_buffer}"]
            - call: [jobs.remove, done_at]
```

`sys.wait_any(procs: list<process>, timeout_ms: i32) -> i32` returns the index of a
process in `procs` that has ended (the lowest such index, `returncode` is set), or `-1`
when none ended within `timeout_ms` (`-1` = no limit, `0` = just look), when the list is
empty, or when a signal caught with `sys.catch_signals` arrived meanwhile.  While it
waits it keeps reading every process's capture pipes — a child writing more than a
pipe holds can not block the pool — and enforces their `timeout`s.  It sleeps in
`poll(2)` on process descriptors (`pidfd`), not in a polling loop.

A `list<process>` owns its processes: `remove(i)` and the list's drop release them like
a `process` variable's drop (pipes closed, the child reaped later, never killed).

## 5.6 `sys.require_tools` — was die Umgebung mitbringen muss

```aiml
- decl: [fehlt, string, { call: [sys.require_tools, "git,gcc>=13,lib:yaml-cpp"] }]
- if: { condition: { gt: [fehlt.len, 0] }, then: [{ print: ["fehlt:${fehlt}"] }] }
```

Eine Zeile, drei Formen, eine Antwort: `""` heisst „alles da", sonst stehen **alle**
fehlenden Eintraege darin, durch Leerzeichen getrennt.

| Form | Bedeutung | Pruefung |
|---|---|---|
| `git` | ein ausfuehrbares Programm | im `PATH`, ausfuehrbar |
| `gcc>=13` | Programm mit Mindestversion | `--version`, erste Zahl, numerisch verglichen |
| `lib:yaml-cpp` | eine Bibliothek | `pkg-config --exists` |

- **F23-40** Die Antwort nennt **alles** Fehlende, nicht das erste.  Eine Pruefung, die beim
  ersten Fehler aufhoert, erzeugt drei Laeufe statt einem — und einen Nutzer, der nach dem
  dritten aufgibt.  Genau dafuer gibt es diesen Aufruf und nicht drei `sys.exe`.
- **F23-41** Fehlt `pkg-config` selbst, gilt jede `lib:`-Anforderung als fehlend und wird
  mit `(pkg-config fehlt)` genannt.  Still „ja" zu sagen waere die gefaehrliche Richtung:
  der Bau liefe los und braeche am Linker ab.
- **F23-42** Die Versionspruefung vergleicht die **erste Zahl** der ersten Ausgabezeile.
  Das genuegt fuer `gcc 13.3.0` und `cmake 3.28` und ist absichtlich einfach; wer mehr
  braucht, prueft selbst.  Was NICHT passiert: raten.  Gibt das Programm keine Zahl her,
  gilt die Anforderung als nicht erfuellt und sagt das (`gcc>=13(hat ?)`).
- **F23-43** Der Aufruf ist **ein** Ort fuer diese Frage.  aiml `spec/62` §18.2 (`requires:`)
  und cosmos (doc/spec/110) rufen ihn beide auf — zwei Kopien derselben Pruefung laufen
  auseinander, und die Haelfte, die schweigt, ist die gefaehrliche.

### 5.7 Signals — `sys.catch_signals`, `sys.caught_signal` (F23-53)

```aiml
- call: [sys.catch_signals, "INT,TERM"]
# … long work, checking now and then …
- if: { condition: { neq: [{ call: [sys.caught_signal] }, 0] }, then: [{ call: [cleanup] }] }
```

| Call | Returns | Meaning |
|---|---|---|
| `sys.catch_signals(names)` | `i32` | From now on the listed signals (`INT`, `TERM`, `HUP`, `QUIT`, `USR1`, `USR2`, `PIPE`, comma-separated) no longer end the program; they are recorded instead. `0` ok, `-1` an unknown name (nothing changed). |
| `sys.caught_signal()` | `i32` | The number of the last caught signal since the previous call (`2` INT, `15` TERM, …), `0` if none; reading clears it. |

There is no callback: a handler only sets a flag (all a signal handler may safely do),
the program decides at a point of its choosing.  Children started by `sys.exe` begin
with every signal at its default, whatever the parent caught.  `sys.wait_any` returns
`-1` early when a caught signal arrives, so a pool loop can stop promptly.


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

### `io.open` Modes

The mode string is passed through to `fopen`, so the C modes apply: `"r"` read,
`"w"` truncate/create, `"a"` **append**, each optionally with `+` (read+write)
and `b` (binary).  `e` (close-on-exec) is added automatically (§5.1), so a
`sys.exe` child never inherits the descriptor.

- `io.write` is `strlen`-based: it stops at a `0x00` and **does not flush**.  For
  binary-safe, immediately flushed writes use `buffer.write_string` +
  `buffer.write_to` (spec/11) — relevant whenever another handle or process reads
  the file while it is still being appended to.

## Proposed Binary File I/O Extensions (Not Implemented Yet)

For large binary model imports, the current text-oriented `io.open/io.read/io.read_all`
API is not sufficient. The following additions are proposed.

Status: **implemented** (see the table below; only `io.read_u64_le` is missing)

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

### Binary Read Functions

Implemented in `src/sys/binary_io_runtime.cpp` and tested by `tests/test_binary_io.aiml`,
except `io.read_u64_le`. Binary output goes through `buffer.write_to(f)` (spec/11).

| Function | Signature | Status | Description |
|----------|-----------|--------|-------------|
| `io.open_binary` | `io.open_binary(path: string\|path) -> file_handle` | implemented | Opens a file in binary read mode. |
| `io.read_bytes` | `io.read_bytes(f: file_handle, n: i32) -> buffer<u8>` | implemented | Reads exactly `n` bytes or fewer at EOF into a byte buffer. |
| `io.tell` | `io.tell(f: file_handle) -> i64` | implemented | Returns current file position in bytes. |
| `io.seek` | `io.seek(f: file_handle, pos: i64) -> void` | implemented | Moves the file position to `pos`. |
| `io.skip` | `io.skip(f: file_handle, delta: i64) -> void` | implemented | Moves the file position forward by `delta`. |
| `io.read_at` | `io.read_at(f: file_handle, offset: i64, n: i32) -> buffer<u8>` | implemented | Reads a byte range without relying on the shared sequential cursor. |
| `io.read_bool` | `io.read_bool(f: file_handle) -> i32` | implemented | Reads one boolean byte and advances the cursor. |
| `io.read_i32_le` | `io.read_i32_le(f: file_handle) -> i32` | implemented | Reads one little-endian `i32` and advances the cursor. |
| `io.read_u32_le` | `io.read_u32_le(f: file_handle) -> i64` | implemented | Reads one little-endian unsigned `u32`. |
| `io.read_i64_le` | `io.read_i64_le(f: file_handle) -> i64` | implemented | Reads one little-endian `i64`. |
| `io.read_u64_le` | `io.read_u64_le(f: file_handle) -> i64` | not implemented | Reads one little-endian unsigned `u64`. |
| `io.read_f32_le` | `io.read_f32_le(f: file_handle) -> f32` | implemented | Reads one IEEE754 `f32`. |
| `io.read_f64_le` | `io.read_f64_le(f: file_handle) -> f64` | implemented | Reads one IEEE754 `f64`. |

### Design Notes

- `io` owns file and stream access.
- `buffer` owns in-memory interpretation of bytes.
- `io.read_at` is especially important for parallel import of very large files
  (for example, multiple workers reading distinct matrix ranges from the same file).

## Process State Semantics

- `process.returncode` is an `i32`, valid once `running()` is `false` (§5.2); there is no
  nullable scalar in aiml.
- A failed spawn returns a `null` process (§5.1); all process operations are null-safe.
