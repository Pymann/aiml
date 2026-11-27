# Tech sheet: timing a program and sampling the peak RSS of its process tree

**Date:** 2026-11-27 · **aiml:** v1 line, master `affec93` (LLVM 18) · **Verdict:** feasible
with the language as it is — no new built-in needed.

## Question

Can `tools/bench_frontends.py` of [Pymann/oculus](https://github.com/Pymann/oculus) (branch
`main`) be written in aiml? The script runs `oculus -p BUILD_DIR --single-rule 40
--frontend json|inproc` per project and frontend, `REPEAT` times, and records per run
the wall time and the **peak resident memory of the whole process tree** (oculus and
its worker processes), sampled from `/proc` every 10 ms. It writes one CSV row per
run: `project, frontend, run, units, seconds, peak_mib`; `units` is the number of
entries in the build directory's `compile_commands.json`.

## What the script needs, and what aiml has

| Python | aiml | Spec |
|---|---|---|
| `subprocess.Popen(argv, cwd=…, stdout=DEVNULL, stderr=DEVNULL)` | `sys.exe(argv, {cwd: dir, stdout: "/dev/null", stderr: "/dev/null"})` | spec/23 §5.1 |
| `proc.pid` | `proc.pid` (`i64`) | spec/23 §5.2 |
| `proc.poll() is None` | `proc.running()` — non-blocking, reaps the child and sets `returncode` | spec/23 §5.2 |
| `proc.returncode` | `proc.returncode` — valid once `running()` is `false` | spec/23 §5.2 |
| `time.monotonic()` | `time.mono_ms()` — milliseconds, enough for runs of seconds | spec/23 |
| `time.sleep(0.01)` | `time.sleep_ms(10)` | spec/23 |
| `os.listdir("/proc/<pid>/task")` | `io.list_dir(...)` → `list<path>`, `entry.name()` | spec/42 |
| `open("/proc/<pid>/status").read()` | `io.read_file(...)` | spec/42, see below |
| `open(".../task/<tid>/children").read().split()` | `io.read_file(...)` + `text.split(" ")` + `word.to_int()` | spec/09 |
| `len(json.load(compile_commands.json))` | `sys.deserialize(text, "list<CompileCommand>")` + `.len` | spec/29 §5.2b |
| `os.environ.get("REPEAT", "3")` | `sys.env` → `map.contains` / `map.get`, `to_int` | spec/23 |
| `"%.3f" % seconds` | `{ cast: [ms, f64] }` / `1000.0` interpolates as `12.345` | spec/02 |
| `csv.writer` | lines built as strings, `io.write_file` | spec/42 |

## `/proc` files report size 0 — `io.read_file` reads them anyway

Virtual files under `/proc` and `/sys` have size 0 in `stat` and still deliver content.
A reader that sizes its buffer from `stat` gets an empty string. `aiml_io_read_file`
(`src/core/path_runtime.cpp`) takes the size only as a hint for the first allocation
and then reads to EOF; for an ordinary file it is still exactly one `fread`. This has
been so since 2026-10-17, after cosmos (`doc/spec/109`) read `/proc/self/stat` and got
nothing.

Checked: `io.read_file("/proc/self/status")` returns 1408 bytes.

## The probe

A shell starts a `python3` that writes 200 MiB, so the memory sits in a grandchild of the
aiml program, as oculus' JSON workers do. The program walks the tree through
`/proc/<pid>/task/<tid>/children` every 10 ms and sums `VmRSS`:

```yaml
- def:
    name: rss_kib                        # VmRSS of one process, 0 when it is gone
    args: [[pid, i64]]
    return: i64
    body:
      - decl: [text, string, { call: [io.read_file, "/proc/${pid}/status"] }]
      - decl: [lines, list<string>, { call: [text.split, "\n"] }]
      - decl: [line_at, i32, 0]
      - while:
          condition: { lt: [line_at, lines.len] }
          body:
            - decl: [line, string, { at: [lines, line_at] }]
            - if:
                condition: { call: [line.starts_with, "VmRSS:"] }
                then:
                  # "VmRSS:\t  217680 kB" splits into empty words too: take the first number
                  - decl: [words, list<string>, { call: [line.split, " "] }]
                  - decl: [word_at, i32, 0]
                  - while:
                      condition: { lt: [word_at, words.len] }
                      body:
                        - decl: [word, string, { at: [words, word_at] }]
                        - decl: [num, i64, { call: [word.to_int] }]
                        - if: { condition: { gt: [num, 0] }, then: [{ return: num }] }
                        - set: [word_at, { +: [word_at, 1] }]
            - set: [line_at, { +: [line_at, 1] }]
      - return: 0

- def:
    name: tree_rss_kib                   # the sum over root and all its descendants
    args: [[root, i64]]
    return: i64
    body:
      - decl: [todo, list<i64>]
      - call: [todo.push, root]
      - decl: [total, i64, 0]
      - decl: [todo_at, i32, 0]
      - while:
          condition: { lt: [todo_at, todo.len] }
          body:
            - decl: [pid, i64, { at: [todo, todo_at] }]
            - set: [total, { +: [total, { call: [rss_kib, pid] }] }]
            - decl: [tasks, list<path>, { call: [io.list_dir, "/proc/${pid}/task"] }]
            - decl: [task_at, i32, 0]
            - while:
                condition: { lt: [task_at, tasks.len] }
                body:
                  - decl: [task, path, { at: [tasks, task_at] }]
                  - decl: [tid, string, { call: [task.name] }]
                  - decl: [kids, string, { call: [io.read_file, "/proc/${pid}/task/${tid}/children"] }]
                  - decl: [kid_words, list<string>, { call: [kids.split, " "] }]
                  - decl: [kid_at, i32, 0]
                  - while:
                      condition: { lt: [kid_at, kid_words.len] }
                      body:
                        - decl: [kid_word, string, { at: [kid_words, kid_at] }]
                        - decl: [kid, i64, { call: [kid_word.to_int] }]
                        - if: { condition: { gt: [kid, 0] }, then: [{ call: [todo.push, kid] }] }
                        - set: [kid_at, { +: [kid_at, 1] }]
                  - set: [task_at, { +: [task_at, 1] }]
            - set: [todo_at, { +: [todo_at, 1] }]
      - return: total

- def:
    name: main
    return: i32
    body:
      - decl: [argv, list<string>, ["sh", "-c", "python3 -c 'import time; b=b\"x\"*(200<<20); time.sleep(0.8)' & sleep 0.1; wait"]]
      - decl: [start, i64, { call: [time.mono_ms] }]
      - decl: [proc, process, { call: [sys.exe, argv, {stdout: "/dev/null", stderr: "/dev/null"}] }]
      - decl: [peak, i64, 0]
      - decl: [samples, i32, 0]
      - while:
          condition: { call: [proc.running] }
          body:
            - decl: [now_kib, i64, { call: [tree_rss_kib, proc.pid] }]
            - if: { condition: { gt: [now_kib, peak] }, then: [{ set: [peak, now_kib] }] }
            - set: [samples, { +: [samples, 1] }]
            - call: [time.sleep_ms, 10]
      - decl: [took, i64, { -: [{ call: [time.mono_ms] }, start] }]
      - print: ["exit ${proc.returncode}: ${took} ms, ${samples} samples, peak ${peak} KiB"]
      - return: 0
```

Result:

```
exit 0: 931 ms, 91 samples, peak 217680 KiB
```

For comparison, `resource.getrusage(RUSAGE_CHILDREN).ru_maxrss` after the same `python3`
run alone reports 215596 KiB. The difference of about 2 MiB is the `sh`. With 91 samples
in 931 ms, the loop keeps the 10 ms rhythm, so the walk over the tree costs little at
this size.

`compile_commands.json` is read as foreign JSON (spec/29 F29-15/16: the type is named in
the call, unknown keys are ignored). A three-entry file with both the `command` and the
`arguments` form gave `units.len == 3`:

```yaml
- model:
    name: CompileCommand
    fields:
      - [directory, string]
      - [file, string]
# …
- decl: [units, list<CompileCommand>, { call: [sys.deserialize, text, "list<CompileCommand>"] }]
```

## Points to mind when porting

- **Names of three characters and more** (spec/49 §23): `i`, `t` and `ws` are compile
  errors.
- **Splitting on `" "`** leaves empty words where `/proc` aligns with tabs and several
  spaces. Take the first word whose `to_int` is greater than 0, or skip the empty ones.
- **No implicit numeric promotion**: write `{ cast: [ms, f64] }`. MiB with one decimal is
  simplest in integers from KiB: `kib / 1024`, then `kib * 10 / 1024 % 10`.
- **A process that ends between two samples** has no `/proc/<pid>` any more.
  `io.read_file` then returns `""` and `io.list_dir` an empty list, so the walk counts it
  as 0, as the Python version's `except OSError` does.
- **`running()` reaps.** Once it has returned `false`, the root's `/proc` entry is gone,
  so the last sample is the one before.
- **Sampling misses peaks shorter than the interval**, in aiml as in Python. That is a
  property of the method, not of the port.

## A built-in peak RSS of a child is no replacement

A built-in such as `getrusage(RUSAGE_CHILDREN)`, or the `rusage` of `wait4` exposed as a
`process.max_rss`, would be simpler and exact. It does not measure the same thing,
though:

- `ru_maxrss` of `RUSAGE_CHILDREN` is the maximum RSS of the **largest single**
  descendant that was waited for, not the sum of the tree at one moment. For
  `--frontend json`, where oculus and its workers hold memory at the same time, the sum
  is the number of interest.
- It is known only after the child has been waited for, not while it runs.

A `process.max_rss` from `wait4` would still be a worthwhile addition: exact for a single
process (`--frontend inproc`) and cheap, as a second column next to the sampled tree sum.
The script does not need it, and neither does CI.

## Files

The probe programs of this sheet were run from `/tmp/benchprobe/` (`tree_rss.aiml`,
`units.aiml`) and are reproduced above. Nothing of them is in the repository's test corpus.
