# 70 — Performance measurement

> Status: **specification of a suite that does not exist yet.** Spec and tests
> precede the implementation (Arbeitsprinzip 3). What exists today is
> `performance/` with five micro cases and one summary from 2026-06-04; §1 says
> why that cannot carry a published claim. Rules carry IDs `F70-n` so a probe can
> name the rule it enforces.

## 1. Why this chapter exists

`performance/results/summary.md` is the current state of the art in this
repository, and no number in it may be published:

* **Five cases**, all micro, none covering persistence, network, dispatch,
  allocation or serialization.
* **AOT stops at the object file** — "full binary linking was not available on
  this machine" — so no AOT run time, no startup time, no binary size exists.
* **`map_put_get` is a 4.87 s outlier** against 0.05 s for its neighbours, which
  the file itself calls "the most suspicious performance issue", unexplained
  since June.
* **The comparison table is a different workload.** C++ 2.15 s, Rust 2.19 s,
  Python ~6 min are n-body and spectral-norm numbers from the Benchmarks Game on
  unknown hardware, printed next to AIML numbers from `loop_sum`. Two workloads
  in one table is not a comparison; it is a category error, and a reviewer will
  say so in one sentence.
* **No environment record**: no CPU, no kernel, no LLVM version, no commit, no
  repetition count, no spread.

A performance claim is a measurement claim. This chapter defines what has to be
measured, under which conditions, and in which form, so that the result survives
someone else re-running it.

## 2. What the pipeline is — the facts that bound every number

Before a single case is written, the measured object must be known. Read from
the implementation on 2026-11-16:

| Property | State |
| --- | --- |
| JIT | MCJIT via `llvm::EngineBuilder` (`compiler.cpp`), **no explicit optimization level** — MCJIT's default applies |
| AOT | `createTargetMachine(triple, cpu, features, TargetOptions, PIC)` + `addPassesToEmitFile` through a `legacy::PassManager`, **no explicit codegen level** |
| **IR optimization** | **none.** The only pass manager in the project exists to emit the object file. No `PassBuilder`, no mem2reg/SROA, no inliner, no GVN — both batteries consume the frontend's IR exactly as emitted |
| Compiler binary | `exodus.json` sets `optimization: "0"`, `debug: true` — the compiler itself is an unoptimized build |
| Clock | `time.now` (epoch ms) and `time.mono_ms` (monotonic ms). **Millisecond resolution only** |
| Memory | `sys.mem_usage`, `sys.mem_diff`, `sys.mem_limit` |
| Store introspection | `db.open_entries`, `db.open_writes`, `db.open_wal_used`, `db.open_count` |
| Cores | `sys.core_count`, `sys.logical_thread_count`, `sys.quota_thread_count` |

**F70-1.** Every result names the pipeline it measured: battery (JIT/AOT/wasm),
AIML commit, LLVM version, and whether an IR optimization pipeline was active.
Once a mid-level pipeline exists (§10), results from before and after it are not
comparable and must not share a baseline file.

**F70-2.** The compiler used for a measurement is built from the same config the
project ships. A measurement taken with a sanitizer build, a `MEM_DIFF` build or
a locally patched compiler is a diagnostic, not a result, and is never written to
`performance/baselines/`.

## 3. Facilities the suite needs first

**F70-3.** A monotonic clock with at least **microsecond** resolution is
required (`time.mono_us`, spec/24). With the present millisecond clock a sample
must be calibrated to at least 500 ms to keep quantisation under 0.2 %, which
makes a 40-case suite needlessly slow and makes per-operation latency
percentiles impossible.

**F70-4.** The AOT path must be measured end to end: `aiml prog.aiml -o prog.o`,
link (`g++` with `prog.o.deps`, `-laiml`), run. A case that stops at the object
file reports `obj_ms` only and is marked incomplete — it may not stand in for
AOT run time.

**F70-5.** Compile-only measurement uses the existing `--check` (compile, do not
run). `total_ms` minus `compile_ms` is never used as a substitute for run time;
both are measured directly.

**F70-6.** Each case is a program that measures itself and prints one JSON
object per sample to stdout (§8). The runner does not time the process from the
outside except for `compile_ms`, `obj_ms`, `link_ms` and `startup_ms`, where the
process boundary *is* the thing being measured.

Shape of the in-program loop (illustrative, not a tested program — the final
form depends on F70-3):

```yaml
- def:
    name: sample
    args: [[reps, i32]]
    return: i64
    body:
      - decl: [t0, i64, { call: [time.mono_ms] }]
      - decl: [i, i32, 0]
      - while:
          condition: { lt: [i, reps] }
          body:
            - call: [workload]
            - set: [i, { +: [i, 1] }]
      - return: { -: [{ call: [time.mono_ms] }, t0] }
```

## 4. Methodology

**F70-10 Calibration.** Each case computes its own repetition count so that one
sample lasts at least 500 ms (ms clock) or 50 ms (µs clock). The count is
reported with the result; a hard-coded count that drifts with hardware is
forbidden.

**F70-11 Warmup.** One discarded sample precedes every measurement series. For
the JIT battery the warmup also establishes the `__aiml_cache` state the case
declares (cold or warm, §6.2).

**F70-12 Repetition.** At least **7** samples per case per battery. Reported:
median, median absolute deviation, min, max, n.

**F70-13 Spread budget.** A case whose MAD exceeds **5 %** of its median is
*noisy*: it is reported, and it may not gate a regression (§8). A case that is
noisy on three consecutive runs is a defect in the case, not in the machine.

**F70-14 Isolation.** One measurement at a time on the machine. No suite, no
sanitizer gate, no build in parallel — the project rule that two `run_all_tests.sh`
runs mix their results applies with more force here. Where available: CPU
affinity pinned (`taskset`), scaling governor `performance`, turbo state
recorded.

**F70-15 Environment record.** Every result file carries: CPU model, core count,
kernel, libc, LLVM version, compiler flags of the AIML build, AIML commit hash,
governor, whether the machine was otherwise idle, and the wall-clock date. A
number without this record is not a result (F70-1).

**F70-16 Determinism of the workload.** A case produces a value that is printed
and checked. Dead code that the backend may delete is not a benchmark; a case
whose result is unused is invalid.

**F70-17 Allocation accounting.** Cases that allocate report `peak_rss_kb` via
`sys.mem_usage` and the net balance via `sys.mem_diff`. A case whose net
balance grows with the repetition count is a leak finding and fails, however
fast it was.

## 5. Metrics

| Metric | Unit | Measured where |
| --- | --- | --- |
| `compile_ms` | ms | `aiml --check` on the case, process boundary |
| `run_ms` | ms | inside the program, calibrated loop |
| `obj_ms` | ms | `aiml -o case.o`, process boundary |
| `link_ms` | ms | linker invocation, process boundary |
| `binary_bytes` | B | size of the linked executable |
| `startup_ms` | ms | AOT binary that prints and exits, process boundary |
| `ops_per_s` | 1/s | repetitions ÷ `run_ms` |
| `p50`, `p95`, `p99` | µs | per-operation latency, requires F70-3 |
| `peak_rss_kb` | kB | `sys.mem_usage` at the end of the case |
| `net_bytes` | B | `sys.mem_diff` over the measured region |

## 6. The workload catalogue

### 6.1 Group A — language core (JIT and AOT, identical sources)

| ID | Case | Measures |
| --- | --- | --- |
| A1 | `loop_sum` | integer loop, reduction |
| A2 | `float_math` | f64 arithmetic, `math.*` calls |
| A3 | `call_overhead` | user function call in a hot loop vs inlined body |
| A4 | `iface_dispatch` | call through `ptr<Iface>` vs direct method |
| A5 | `string_build` | repeated growth, `${}` interpolation separately |
| A6 | `list_ops` | `push`, `at`, `len` over 10⁴…10⁷ elements |
| A7 | `map_put_get` | `map<string:i32>` and `map<string:Model>`; **carries the 4.87 s outlier until it is explained** |
| A8 | `sort` | `list<i32>`, `list<string>`, `list<f64>`, 10⁴…10⁶ |
| A9 | `struct_array_walk` | `array<T:N>` and `list<Model>` traversal |
| A10 | `alloc_churn` | `new`/`free` of a model in a loop; with and without `scope` |
| A11 | `exceptions` | `throw`/`catch` cost, and the cost of a `try` that never throws |
| A12 | `serialize` | `sys.serialize`/`deserialize` round trip by payload shape |

**F70-30.** Every Group A case runs under **both** batteries from the same
source file, and the result file carries both. A case that only runs under one
battery is incomplete.

### 6.2 Group B — the JIT battery

| ID | Case | Measures |
| --- | --- | --- |
| B1 | `compile_scale` | `compile_ms` over 100 / 1 000 / 10 000 lines of generated but idiomatic source |
| B2 | `cache_cold_warm` | same file with empty vs populated `__aiml_cache` |
| B3 | `import_closure` | compile time over an import graph of 1 / 8 / 64 files |
| B4 | `time_to_first_output` | process start → first byte on stdout |
| B5 | `compile_rss` | peak RSS of the compiler for B1's largest input |

**F70-31.** B2 states which cache state it measured and never deletes
`__aiml_cache` by hand to produce a cold state — the cache invalidates itself on
compiler mtime; a manual delete measures a situation that does not occur.

### 6.3 Group C — the AOT battery

| ID | Case | Measures |
| --- | --- | --- |
| C1 | `obj_emit` | `obj_ms` for the A-cases |
| C2 | `link_cost` | `link_ms`, and how it scales with the `.deps` count |
| C3 | `binary_size` | `binary_bytes` for a hello-world and for A12 |
| C4 | `startup` | `startup_ms` of a binary that prints and exits |
| C5 | `jit_aot_parity` | A-case run times under both batteries, side by side |

**F70-32.** C5 is a **correctness gate before it is a performance case**: both
batteries must print byte-identical output. A divergence is a compiler finding
and is reported as such, never as a performance difference.

### 6.4 Group D — Kv, Store, VecStore, TimeSeries (spec/31, spec/45)

| ID | Case | Measures |
| --- | --- | --- |
| D1 | `kv_put_get` | `.set`/`.get` throughput at value sizes 16 B, 256 B, 4 KiB, 64 KiB |
| D2 | `kv_scale` | the same at 10³, 10⁵, 10⁶ keys — throughput and `peak_rss_kb` |
| D3 | `kv_persist` | `{persist: true}` vs in-memory; `db.open_wal_used` growth per write |
| D4 | `kv_checkpoint` | `.checkpoint` duration by dirty-set size, and the read path **after** the checkpoint (spec/31 §6.2.1 hands over the RAM index) |
| D5 | `kv_indexed` | cost per `.set` with 0 / 1 / 4 secondary indexes — the O(1) maintenance claim is a measurable claim |
| D6 | `kv_decoded_cache` | `.get` hit vs miss, and behaviour under cache pressure around the documented 900/1200-row boundary (spec/31 §6.7.3) |
| D7 | `kv_reopen` | open time and first-read latency for a store of 10⁶ keys |
| D8 | `store_query` | `.query` over an entity store, with and without an index |
| D9 | `vec_search` | `.search` latency and recall by dimension (64/384/1536) and k (1/10/100) |
| D10 | `ts_append_range` | `.append` throughput, `range` read of 10⁶ events |

**F70-33.** Every D case states its durability mode. A persistence benchmark
that silently measured an in-memory store is a false claim; `db.open_wal_used`
before and after is the proof that writes reached the log.

**F70-34.** D cases run in a scratch directory that is created and removed by
the case. A measurement against the live store is forbidden — the same rule as
`./rpc_gate` never being started bare.

### 6.5 Group E — networking (spec/32)

| ID | Case | Measures |
| --- | --- | --- |
| E1 | `http_throughput` | `http.get` against the in-process `http.server`, keep-alive vs fresh connection |
| E2 | `http_concurrency` | the same at 1 / 8 / 64 concurrent clients |
| E3 | `http_payload` | 1 KiB / 1 MiB / 16 MiB response bodies |
| E4 | `http_latency` | p50/p95/p99 per request (requires F70-3) |
| E5 | `tls_handshake` | `http.server_tls`: handshake cost, and throughput once established |
| E6 | `ws_message_rate` | `ws.send` of 64 B frames, messages per second |
| E7 | `ws_echo_rtt` | round-trip latency, percentiles |
| E8 | `ws_kv_blob` | `ws.send_kv_blob` vs an equivalent `sys.serialize` + `ws.send_binary` |
| E9 | `socket_baseline` | the same payload over raw `net.send`/`net.recv` |

**F70-35.** E1–E8 report the `net` baseline from E9 alongside, so protocol cost
is separated from syscall cost. A network number without its raw-socket
baseline cannot distinguish a slow protocol implementation from a slow machine.

**F70-36.** Networking cases run against **localhost** and say so. A number over
a real link measures the link. The frame cap (64 MiB, `ws_recv_frame_raw`) is a
boundary case, not a throughput case: it is measured once, for the rejection
path.

## 7. Baselines in other languages

**F70-60.** A cross-language comparison is valid only when the baseline
implements the **same algorithm**, produces the **same output**, runs on the
**same machine**, and is measured in the **same run** by the same runner.

**F70-61.** Published third-party numbers are never used as a comparison. They
may be cited as context, in a separate section, named as a different workload on
unknown hardware — which is exactly what the present `summary.md` fails to do.

**F70-62.** Baseline set per group: Group A against C (`clang -O2`) and Python
3.12, optionally Rust (release). Group D against SQLite and LMDB where the
operation is comparable. Group E against a minimal server in C or Go. The
baseline sources live in `performance/baselines/<case>/` and are built by the
runner, not by hand.

**F70-63.** The baseline's build flags are part of the result. "AIML vs C"
without the C flags is not a comparison.

## 8. Result format and the regression gate

**F70-70.** One JSON object per sample on stdout, one result file per case and
run under `performance/results/<case>/<run-id>.json`:

```json
{
  "case": "A7_map_put_get", "battery": "aot", "reps": 200000,
  "samples_ms": [512, 509, 515, 511, 508, 514, 510],
  "median_ms": 511, "mad_ms": 2, "min_ms": 508, "max_ms": 515, "n": 7,
  "ops_per_s": 391389, "peak_rss_kb": 18244, "net_bytes": 0,
  "env": { "commit": "...", "llvm": "18.1.3", "cpu": "...", "cores": 8,
           "kernel": "...", "governor": "performance", "ir_opt": "none" }
}
```

**F70-71.** `performance/baselines/<case>.json` holds the accepted median per
battery. The gate fails a case whose median is **more than 10 %** worse than its
baseline, unless the case is noisy (F70-13). An improvement never fails, but
an improvement above 25 % requires the baseline to be rewritten in the same
commit — a silent jump is usually a broken case, not a win.

**F70-72.** `performance/results/summary.md` is **generated**, never
hand-written, and carries the environment record of its run.

**F70-73.** The suite has a single entry point (`performance/run.sh`) with
`--only <pattern>`, `--battery jit|aot|both` and `--baseline` to re-record. It
never runs as part of `run_all_tests.sh`: a functional suite that also measures
is neither.

## 9. Forbidden, because each of these has already produced a wrong number

**F70-80.** Compile time inside run time, or either one reported as "the"
duration.

**F70-81.** Comparing against a foreign workload (§7, F70-61).

**F70-82.** Measuring a debug, sanitizer or `-O0`-generated-code build and
calling the result performance (F70-2). The deployed web client was measured in
a `-O0` build once; the lesson is in `doc/`.

**F70-83.** A median without its spread, or a single run presented as a result.

**F70-84.** A case that passes while its memory balance grows (F70-17).

**F70-85.** Deleting `__aiml_cache` by hand to manufacture a cold start
(F70-31).

## 10. What the suite unlocks

§2 records that **no IR optimization pipeline runs**. Every Group A number
therefore measures the quality of the emitted IR plus the LLVM backend, not an
optimizing compiler. Adding a mid-level pipeline (`PassBuilder` at a chosen
`OptimizationLevel`) is very likely the single largest performance lever this
project has — and it is unmeasurable until this suite exists.

That is the order of work: suite first, baselines recorded on the current
pipeline, then the pipeline, then the same suite again. Done the other way
round, the gain is a feeling.

Open questions, deliberately not decided here:

* Which `OptimizationLevel` becomes the default per battery, and whether the JIT
  and the AOT path share it.
* Whether `peak_rss_kb` should come from the process (`/proc/self/status`,
  VmHWM) instead of `sys.mem_usage`, which measures the allocator's view.
* Whether Group E needs a second machine to be honest about the network, or
  whether localhost plus the raw-socket baseline (F70-35) is sufficient for the
  claims this project wants to make.
