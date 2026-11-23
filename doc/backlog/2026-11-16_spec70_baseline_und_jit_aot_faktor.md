# Performance: complete the A baseline and explain the JIT/AOT factor of 2

Date: 2026-11-16
Status: backlog (both can wait — classified that way by the user)
Spec: `spec/70_performance.md`
First measuring point: commit `929c80ff`, `performance/cases/fib100.aiml`

## Why the two belong together

The order from spec/70 §10 is: suite, baselines on today's
pipeline, **then** the IR middle layer, then the same suite. Both points here
are the "baselines" piece. Whoever builds the `PassBuilder` pipeline first has
no "before" afterwards.

## Point 1 — the JIT is 1.9× slower than the AOT binary

Measured on `fib100` (Ryzen 7 5700U, governor powersave, LLVM 18.1.3,
`ir_opt=none`, median of 7 samples, calibration ≥500 ms):

| Battery | ns per round (100 steps) | reps | Median |
| --- | --- | --- | --- |
| JIT | 455 | 2,048,000 | 932 ms |
| AOT | 245 | 4,096,000 | 1,005 ms |
| C (gcc 13.3 -O2) | 22 | 32,768,000 | 748 ms |

Both batteries compile **the same IR** and set **no**
optimisation level explicitly:

* JIT: `llvm::EngineBuilder` without `setOptLevel` (`compiler.cpp:1559`, `:1705`)
* AOT: `createTargetMachine(triple, cpu, features, TargetOptions, PIC)` without
  `CodeGenOptLevel`, then `addPassesToEmitFile` via a
  `legacy::PassManager` (`compiler.cpp:2106`, `:2118`)

The outputs are identical — it is **not** a correctness finding (spec/70 C5),
but a factor of 2 without a stated reason. To check:

1. Which `CodeGenOptLevel` does `EngineBuilder` actually assume in LLVM 18,
   and which one the default `TargetMachine`? If they differ, that is
   the explanation — and then the level belongs written down **explicitly** in both places,
   instead of relying on defaults (a default you
   cannot see changes silently with an LLVM upgrade).
2. Is it instead the relocation (PIC in AOT) or symbol resolution via
   `DynamicLibrary` in the JIT?
3. Does the factor apply only to this loop or across the whole A group? That
   decides whether it is a codegen topic or a call overhead.

## Point 2 — the remaining A cases (spec/70 §6.1)

`fib100` covers A1 (`loop_sum`). A2–A12 are open: `float_math`,
`call_overhead`, `iface_dispatch`, `string_build`, `list_ops`, `map_put_get`
(carries the unexplained 4.87 s outlier of the old measurement), `sort`,
`struct_array_walk`, `alloc_churn` (with/without `scope`), `exceptions`,
`serialize`.

For each case, what `fib100` already shows applies: its own calibration, seven samples,
a correctness anchor, the C baseline in the same run, both batteries from the same
source (F70-30).

## What is missing beforehand

* **`time.mono_us`** (F70-3, spec/24). With milliseconds every sample needs
  ≥500 ms; with twelve cases × two batteries × eight samples that alone is
  ~2.5 minutes of pure measuring time, and latency percentiles (groups D/E) are
  impossible.
* **JSON output in the runner** (F70-70). `performance/run_case.sh` measures correctly
  but still writes text; without `results/<case>/<run-id>.json` there is no
  regression gate (F70-71).
* **`performance/run.sh` is broken for AOT** — it passes `-o <case>.bin`,
  gets an object file and tries to execute it; with `set -e` it ends there.
  Either switch it over to `run_case.sh` or replace it.

## Side finding that remains

One sample per JIT series drops ~27 % low (678 ms against a median of 932,
reproduced in both runs). Under `powersave` probably boost behaviour;
`performance` as the governor needs root. As long as that is not explained, the
JIT figure is borderline per F70-13 (the MAD does stay at 0.6 %, because the
outlier stays a single one).
